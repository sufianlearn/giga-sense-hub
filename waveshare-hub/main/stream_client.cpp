/**
 * @file stream_client.cpp
 * @brief MJPEG stream client — connects to ESP32 camera nodes.
 *        Runs in dedicated FreeRTOS tasks (one per camera).
 */
#include "stream_client.h"
#include "hmac_auth.h"
#include "definitions.h"

#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <stdlib.h>

#define STREAM_PORT    80
#define STREAM_PATH    "/stream"
#define NODE_INFO_PATH "/info"
#define MJPEG_BOUNDARY "frame"

camera_node_t g_nodes[2];
int           g_active_node_count = 0;
uint8_t      *g_jpeg_buf[2] = {NULL, NULL};
int           g_jpeg_len[2] = {0, 0};
bool          g_frame_ready[2] = {false, false};
uint16_t     *g_rgb_buf[2] = {NULL, NULL};
bool          g_rgb_ready[2] = {false, false};
static bool   s_stream_client_initialized = false;

void stream_client_init(void)
{
    if (s_stream_client_initialized) {
        ESP_LOGI(TAG, "Stream client already initialized");
        return;
    }

    memset(g_nodes, 0, sizeof(g_nodes));
    g_active_node_count = 0;

    for (int i = 0; i < MAX_NODES; i++) {
        g_nodes[i].node_id = i;
        g_nodes[i].active = false;
        g_nodes[i].stream_connected = false;
        g_nodes[i].sock = -1;
        snprintf(g_nodes[i].ip, sizeof(g_nodes[i].ip), "192.168.4.%d", 2 + i);
        /* Allocate JPEG buffer in PSRAM */
        g_jpeg_buf[i] = (uint8_t *)heap_caps_malloc(JPEG_BUF_SIZE, MALLOC_CAP_SPIRAM);
        /* Allocate RGB565 buffer in PSRAM (320x240x2 = 153600 bytes) */
        g_rgb_buf[i] = (uint16_t *)heap_caps_malloc(CAM_FRAME_W * CAM_FRAME_H * 2, MALLOC_CAP_SPIRAM);

        if (!g_jpeg_buf[i] || !g_rgb_buf[i]) {
            ESP_LOGE(TAG, "Stream buffer allocation failed for node %d jpeg=%p rgb=%p",
                     i, g_jpeg_buf[i], g_rgb_buf[i]);
            abort();
        }
    }

    s_stream_client_initialized = true;
    ESP_LOGI(TAG, "Stream client initialized for %d nodes", MAX_NODES);
}

bool stream_client_is_initialized(void) { return s_stream_client_initialized; }

static int read_line(int sock, char *buf, int maxlen, int timeout_ms)
{
    int pos = 0;
    struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (pos < maxlen - 1) {
        char ch;
        int r = recv(sock, &ch, 1, 0);
        if (r <= 0) return -1;
        if (ch == '\n') {
            buf[pos] = '\0';
            if (pos > 0 && buf[pos - 1] == '\r') buf[--pos] = '\0';
            return pos;
        }
        buf[pos++] = ch;
    }
    buf[pos] = '\0';
    return pos;
}

void scan_for_nodes(void)
{
    if (!s_stream_client_initialized) return;
    int found = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(STREAM_PORT);
        inet_pton(AF_INET, g_nodes[i].ip, &addr.sin_addr);

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) continue;

        /* Short connect timeout */
        struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            close(sock);
            if (g_nodes[i].active) {
                ESP_LOGI(TAG, "Node %d lost", i);
                g_nodes[i].active = false;
            }
            continue;
        }

        /* Send /info request with HMAC */
        char hmac[65], nonce[16];
        hmac_sign(NODE_INFO_PATH, hmac, nonce);
        char req[512];
        snprintf(req, sizeof(req),
            "GET %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "X-GSH-Auth: %s\r\n"
            "X-GSH-Nonce: %s\r\n"
            "\r\n",
            NODE_INFO_PATH, g_nodes[i].ip, hmac, nonce);
        send(sock, req, strlen(req), 0);

        /* Read response */
        char line[256];
        bool in_body = false;
        char body[512] = "";
        while (read_line(sock, line, sizeof(line), 1000) >= 0) {
            if (!in_body && strlen(line) == 0) { in_body = true; continue; }
            if (in_body) strncat(body, line, sizeof(body) - strlen(body) - 1);
        }
        close(sock);

        /* Parse nodeId */
        char *nid_ptr = strstr(body, "\"nodeId\":");
        if (nid_ptr) {
            int nid = atoi(nid_ptr + 9);
            if (nid >= 0 && nid < MAX_NODES) {
                if (!g_nodes[nid].active) {
                    ESP_LOGI(TAG, "Found node %d at %s", nid, g_nodes[i].ip);
                }
                g_nodes[nid].active = true;
                strncpy(g_nodes[nid].ip, g_nodes[i].ip, sizeof(g_nodes[nid].ip));
                found++;
            }
        }
    }
    g_active_node_count = found;
}

bool connect_stream(int node_idx)
{
    if (!s_stream_client_initialized) return false;
    if (node_idx < 0 || node_idx >= MAX_NODES) return false;
    camera_node_t &node = g_nodes[node_idx];
    if (!node.active) return false;
    if (node.stream_connected && node.sock >= 0) return true;

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(STREAM_PORT);
    inet_pton(AF_INET, node.ip, &addr.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "Stream connect to node %d failed", node_idx);
        close(sock);
        return false;
    }

    char hmac[65], nonce[16];
    hmac_sign(STREAM_PATH, hmac, nonce);
    char req[512];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: keep-alive\r\n"
        "X-GSH-Auth: %s\r\n"
        "X-GSH-Nonce: %s\r\n"
        "\r\n",
        STREAM_PATH, node.ip, hmac, nonce);
    send(sock, req, strlen(req), 0);

    /* Wait for boundary */
    char line[128];
    char boundary_marker[64];
    snprintf(boundary_marker, sizeof(boundary_marker), "--%s", MJPEG_BOUNDARY);

    for (int tries = 0; tries < 50; tries++) {
        if (read_line(sock, line, sizeof(line), 200) > 0) {
            if (strncmp(line, boundary_marker, strlen(boundary_marker)) == 0) {
                node.sock = sock;
                node.stream_connected = true;
                ESP_LOGI(TAG, "Stream connected to node %d", node_idx);
                return true;
            }
        }
    }

    close(sock);
    return false;
}

bool read_frame(int node_idx)
{
    if (!s_stream_client_initialized) return false;
    if (node_idx < 0 || node_idx >= MAX_NODES) return false;
    camera_node_t &node = g_nodes[node_idx];
    if (!node.stream_connected || node.sock < 0) return false;

    char line[128];
    int clen = -1;

    /* Read headers until empty line */
    while (true) {
        int n = read_line(node.sock, line, sizeof(line), 3000);
        if (n < 0) { disconnect_stream(node_idx); return false; }
        if (n == 0) break;
        if (strncmp(line, "Content-Length:", 15) == 0) {
            clen = atoi(line + 15);
        }
    }

    if (clen <= 0 || clen > JPEG_BUF_SIZE) return false;

    /* Read JPEG body */
    int rd = 0;
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(node.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (rd < clen) {
        int r = recv(node.sock, g_jpeg_buf[node_idx] + rd, clen - rd, 0);
        if (r <= 0) { disconnect_stream(node_idx); return false; }
        rd += r;
    }

    g_jpeg_len[node_idx] = clen;
    g_frame_ready[node_idx] = true;

    /* Read until next boundary */
    char boundary_marker[64];
    snprintf(boundary_marker, sizeof(boundary_marker), "--%s", MJPEG_BOUNDARY);
    for (int tries = 0; tries < 20; tries++) {
        int n = read_line(node.sock, line, sizeof(line), 1000);
        if (n > 0 && strncmp(line, boundary_marker, strlen(boundary_marker)) == 0) break;
    }

    return true;
}

void disconnect_stream(int node_idx)
{
    if (node_idx < 0 || node_idx >= MAX_NODES) return;
    camera_node_t &node = g_nodes[node_idx];
    if (node.sock >= 0) {
        close(node.sock);
        node.sock = -1;
    }
    node.stream_connected = false;
    g_frame_ready[node_idx] = false;
    ESP_LOGI(TAG, "Stream disconnected from node %d", node_idx);
}
