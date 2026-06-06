/**
 * @file main.cpp
 * @brief GigaSenseHub — Waveshare ESP32-S3 7" Display Hub
 *
 *        Entry point: initialise hardware, WiFi AP, launch streaming tasks,
 *        build UI screens, run LVGL loop.
 *
 *        Architecture:
 *          - WiFi SoftAP mode (camera nodes connect to us)
 *          - 2 FreeRTOS tasks for camera streaming (one per node)
 *          - 1 task for weather updates
 *          - LVGL task handles UI rendering
 */
#include "lvgl_port.h"
#include "wifi_ap.h"
#include "stream_client.h"
#include "jpeg_decoder.h"
#include "weather.h"
#include "ui_login.h"
#include "ui_dashboard.h"
#include "ui_settings.h"
#include "definitions.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"

/* Screens */
static lv_obj_t *scr_login    = NULL;
static lv_obj_t *scr_dashboard = NULL;
static lv_obj_t *scr_settings  = NULL;

/* FPS tracking per camera */
static int      s_fps[2]        = {0, 0};
static int      s_frame_cnt[2]  = {0, 0};
static uint32_t s_fps_tick[2]   = {0, 0};
static bool     s_runtime_tasks_started = false;
static SemaphoreHandle_t s_scan_mutex = NULL;

static void cam_stream_task(void *arg);
static void status_task(void *arg);

/* ── Screen navigation (called from UI modules) ──────────── */

extern "C" void app_switch_to_dashboard(void)
{
    if (lvgl_port_lock(-1)) {
        if (!scr_dashboard) scr_dashboard = ui_dashboard_create();
        lv_scr_load(scr_dashboard);
        lv_obj_invalidate(lv_scr_act());
        lvgl_port_unlock();
    }

    if (!s_runtime_tasks_started) {
        if (!wifi_ap_is_started() || !stream_client_is_initialized()) {
            ESP_LOGE(TAG, "Runtime start blocked: wifi=%d stream=%d",
                     wifi_ap_is_started(), stream_client_is_initialized());
            return;
        }

        s_runtime_tasks_started = true;
        s_scan_mutex = xSemaphoreCreateMutex();
        if (!s_scan_mutex) {
            ESP_LOGE(TAG, "Failed to create scan mutex");
            return;
        }
        xTaskCreatePinnedToCore(cam_stream_task, "cam0", 8192,
                                (void *)0, 4, NULL, 1);
        xTaskCreatePinnedToCore(cam_stream_task, "cam1", 8192,
                                (void *)1, 4, NULL, 1);
        xTaskCreate(status_task, "status", 4096, NULL, 2, NULL);
        /* Weather remains disabled in SoftAP-only camera mode. */
        ESP_LOGI(TAG, "Wireless camera runtime tasks launched");
    }
}

extern "C" void app_switch_to_settings(void)
{
    if (lvgl_port_lock(-1)) {
        if (!scr_settings) scr_settings = ui_settings_create();
        lv_scr_load(scr_settings);
        lvgl_port_unlock();
    }
}

/* ── Camera stream task (one per node) ────────────────────── */

static void cam_stream_task(void *arg)
{
    int idx = (int)(intptr_t)arg;
    ESP_LOGI(TAG, "Camera stream task %d started", idx);

    /* Wait for WiFi AP to be up */
    while (!wifi_ap_is_started()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    uint32_t scan_tick = 0;

    while (true) {
        /* Periodic node scan — mutex prevents concurrent /info requests
           that overwhelm the single-threaded Arduino WebServer on camera nodes */
        if (xTaskGetTickCount() - scan_tick > pdMS_TO_TICKS(5000)) {
            if (xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                scan_for_nodes();
                xSemaphoreGive(s_scan_mutex);
            }
            scan_tick = xTaskGetTickCount();
        }

        if (!g_nodes[idx].active) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Connect if needed */
        if (!g_nodes[idx].stream_connected) {
            if (!connect_stream(idx)) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }

        /* Read frame */
        if (read_frame(idx)) {
            /* Decode JPEG → RGB565 */
            if (g_jpeg_buf[idx] && g_rgb_buf[idx]) {
                int w = 0, h = 0;
                if (jpeg_decode_to_rgb565(g_jpeg_buf[idx], g_jpeg_len[idx],
                                           g_rgb_buf[idx], &w, &h)) {
                    g_rgb_ready[idx] = true;

                    /* Update UI with decoded frame */
                    if (lvgl_port_lock(50)) {
                        ui_dashboard_update_cam(idx, g_rgb_buf[idx], w, h);
                        lvgl_port_unlock();
                    }

                    /* FPS counter */
                    s_frame_cnt[idx]++;
                    uint32_t now = xTaskGetTickCount();
                    if (now - s_fps_tick[idx] >= pdMS_TO_TICKS(1000)) {
                        s_fps[idx] = s_frame_cnt[idx];
                        s_frame_cnt[idx] = 0;
                        s_fps_tick[idx] = now;
                    }
                }
            }
            g_frame_ready[idx] = false;
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ── Status update task ───────────────────────────────────── */

static void status_task(void *arg)
{
    while (true) {
        if (lvgl_port_lock(50)) {
            ui_dashboard_update_status();
            ui_dashboard_update_weather();
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ── Entry point ──────────────────────────────────────────── */

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "GigaSenseHub Waveshare — starting");

    /* 1. Display + touch + LVGL init */
    lvgl_port_init();
    lvgl_port_start_task();

    /* 2. WiFi Access Point — camera nodes connect to this SoftAP. */
    wifi_ap_init();

    /* 3. Stream client buffers/state. Runtime tasks start after PIN. */
    stream_client_init();

    /* 4. Build UI screens — only create login initially.
       Dashboard/settings created lazily on first navigation
       to avoid PSRAM bandwidth contention with RGB LCD DMA. */
    if (lvgl_port_lock(-1)) {
        scr_login = ui_login_create();
        /* Show login first */
        lv_scr_load(scr_login);
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Login shown; runtime tasks start after PIN");
}
