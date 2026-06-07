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
#include "power_mgmt.h"
#include "settings.h"
#include "i18n.h"
#include "ui_toast.h"
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
static void weather_task(void *arg);

/* ── Screen navigation (called from UI modules) ──────────── */

extern "C" void app_switch_to_dashboard(void)
{
    if (lvgl_port_lock(-1)) {
        if (!scr_dashboard) scr_dashboard = ui_dashboard_create();
        lv_screen_load_anim(scr_dashboard, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
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
                                (void *)0, 3, NULL, 1);
        xTaskCreatePinnedToCore(cam_stream_task, "cam1", 8192,
                                (void *)1, 3, NULL, 1);
        xTaskCreate(status_task, "status", 4096, NULL, 2, NULL);
        xTaskCreate(weather_task, "weather", 8192, NULL, 2, NULL);
        ESP_LOGI(TAG, "Runtime tasks launched (cam0, cam1, status, weather)");
    }
}

extern "C" void app_switch_to_settings(void)
{
    if (lvgl_port_lock(-1)) {
        /* Always recreate settings to show fresh node/network status */
        if (scr_settings) {
            lv_obj_delete(scr_settings);
            scr_settings = NULL;
        }
        scr_settings = ui_settings_create();
        lv_screen_load_anim(scr_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
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

                    /* Yield to let RGB LCD DMA finish current frame
                       before we touch PSRAM via LVGL flush */
                    vTaskDelay(1);

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

            /* Rate-limit: ~15 FPS max per camera to keep PSRAM bus
               available for RGB LCD DMA refresh. Without this, two
               cameras streaming at full speed starve the display DMA. */
            vTaskDelay(pdMS_TO_TICKS(33));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ── Status update task ───────────────────────────────────── */

static void status_task(void *arg)
{
    bool prev_node_state[2] = {false, false};

    while (true) {
        /* Power management tick */
        power_mgmt_tick();

        if (lvgl_port_lock(50)) {
            ui_dashboard_update_status();
            ui_dashboard_update_weather();

            /* Toast on camera connect/disconnect */
            for (int i = 0; i < MAX_NODES; i++) {
                if (g_nodes[i].active != prev_node_state[i]) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Camera %d %s", i,
                             g_nodes[i].active ? "connected" : "disconnected");
                    ui_toast_show(msg,
                        g_nodes[i].active ? TOAST_SUCCESS : TOAST_WARNING, 3000);
                    prev_node_state[i] = g_nodes[i].active;
                }
            }
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ── Weather fetch task ───────────────────────────────────── */

static void weather_task(void *arg)
{
    ESP_LOGI(TAG, "Weather task started, waiting for STA...");

    /* Wait for STA to connect to home router */
    while (!wifi_sta_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "STA connected, starting weather fetches");

    while (true) {
        if (weather_fetch()) {
            ESP_LOGI(TAG, "Weather updated successfully");
        } else {
            ESP_LOGW(TAG, "Weather fetch failed, will retry");
        }
        /* Fetch every 15 minutes */
        vTaskDelay(pdMS_TO_TICKS(15 * 60 * 1000));
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

    /* 3. Persistent settings from NVS */
    settings_init();
    i18n_set_language((lang_t)settings_get_language());

    /* 4. Power management */
    power_mgmt_init();
    power_mgmt_set_brightness(settings_get_brightness());
    power_mgmt_set_dim_timeout(settings_get_dim_timeout());
    power_mgmt_set_off_timeout(settings_get_off_timeout());

    /* 5. Stream client buffers/state. Runtime tasks start after PIN. */
    stream_client_init();

    /* 6. Weather module init */
    weather_init();

    /* 7. Toast notification system */
    ui_toast_init();

    /* 4. Build UI screens — only create login initially.
       Dashboard/settings created lazily on first navigation
       to avoid PSRAM bandwidth contention with RGB LCD DMA. */
    if (lvgl_port_lock(-1)) {
        scr_login = ui_login_create();
        /* Show login first */
        lv_screen_load(scr_login);
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Login shown; runtime tasks start after PIN");
}
