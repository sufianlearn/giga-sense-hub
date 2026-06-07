#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int    node_id;
    char   ip[16];
    bool   active;
    bool   stream_connected;
    int    rssi;
    int    sock;           /* TCP socket for MJPEG stream */
} camera_node_t;

/* Shared state */
extern camera_node_t g_nodes[2];
extern int           g_active_node_count;

/* JPEG frame buffers (one per camera for dual view) */
extern uint8_t *g_jpeg_buf[2];
extern int      g_jpeg_len[2];
extern bool     g_frame_ready[2];

/* RGB565 decoded frame buffers */
extern uint16_t *g_rgb_buf[2];
extern bool      g_rgb_ready[2];

void stream_client_init(void);
bool stream_client_is_initialized(void);
void scan_for_nodes(void);
bool connect_stream(int node_idx);
bool read_frame(int node_idx);
void disconnect_stream(int node_idx);

#ifdef __cplusplus
}
#endif
