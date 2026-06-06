/**
 * @file ui_dashboard.cpp
 * @brief Dashboard with dual camera live view + weather panel + status bar.
 *        Layout: 800x480
 *        ┌────────────────────────────────────────────────────┐
 *        │ Status Bar (30px)                                  │
 *        ├──────────────┬──────────────┬──────────────────────┤
 *        │  Cam 0       │  Cam 1       │  Weather Panel       │
 *        │  (320x240)   │  (320x240)   │  (160x420)           │
 *        │              │              │  Temperature         │
 *        │              │              │  Description         │
 *        │              │              │  Wind Speed          │
 *        │              │              │  City                │
 *        ├──────────────┴──────────────┤  Settings btn        │
 *        │  FPS + Node Info            │                      │
 *        └──────────────────────────────┴──────────────────────┘
 */
#include "ui_dashboard.h"
#include "stream_client.h"
#include "weather.h"
#include "wifi_ap.h"
#include "definitions.h"
#include "esp_log.h"
#include <stdio.h>

static lv_obj_t *scr_dash = NULL;

/* Camera image widgets */
static lv_obj_t *cam_img[2]  = {NULL, NULL};
static lv_image_dsc_t cam_dsc[2];

/* Status bar */
static lv_obj_t *lbl_status  = NULL;

/* Weather panel */
static lv_obj_t *lbl_temp    = NULL;
static lv_obj_t *lbl_desc    = NULL;
static lv_obj_t *lbl_wind    = NULL;
static lv_obj_t *lbl_city    = NULL;
static lv_obj_t *lbl_weather_icon = NULL;

/* FPS */
static lv_obj_t *lbl_fps     = NULL;

/* Forward declare */
extern "C" void app_switch_to_settings(void);

static void no_scroll(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void opaque_label(lv_obj_t *lbl, uint32_t bg)
{
    if (!lbl) return;
    lv_obj_set_style_bg_color(lbl, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(lbl, 2, 0);
    lv_obj_set_style_pad_right(lbl, 2, 0);
    no_scroll(lbl);
}

static void invalidate_dashboard(void)
{
    if (scr_dash) lv_obj_invalidate(scr_dash);
}

static void settings_btn_cb(lv_event_t *e)
{
    (void)e;
    app_switch_to_settings();
}

lv_obj_t *ui_dashboard_create(void)
{
    scr_dash = lv_obj_create(NULL);
    lv_obj_set_size(scr_dash, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(scr_dash, lv_color_hex(0x0f0f23), 0);
    lv_obj_set_style_bg_opa(scr_dash, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_dash, 0, 0);
    no_scroll(scr_dash);

    /* ── Status Bar ─────────────────────────────────────────── */
    lv_obj_t *bar = lv_obj_create(scr_dash);
    lv_obj_set_size(bar, 784, 32);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 8, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    no_scroll(bar);

    lbl_status = lv_label_create(bar);
    lv_label_set_text(lbl_status, LV_SYMBOL_WIFI "  GigaSenseHub  |  Nodes: 0  |  WiFi: AP");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_width(lbl_status, 760);
    lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_CLIP);
    opaque_label(lbl_status, 0x16213e);
    lv_obj_align(lbl_status, LV_ALIGN_LEFT_MID, 8, 0);

    /* ── Camera 0 ───────────────────────────────────────────── */
    /* Card container */
    lv_obj_t *cam0_card = lv_obj_create(scr_dash);
    lv_obj_set_size(cam0_card, 314, 260);
    lv_obj_set_pos(cam0_card, 8, 36);
    lv_obj_set_style_bg_color(cam0_card, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(cam0_card, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(cam0_card, 1, 0);
    lv_obj_set_style_radius(cam0_card, 0, 0);
    lv_obj_set_style_pad_all(cam0_card, 2, 0);
    no_scroll(cam0_card);

    lv_obj_t *cam0_lbl = lv_label_create(cam0_card);
    lv_label_set_text(cam0_lbl, LV_SYMBOL_VIDEO " Cam 0");
    lv_obj_set_style_text_color(cam0_lbl, lv_color_hex(0x00d4ff), 0);
    lv_obj_align(cam0_lbl, LV_ALIGN_TOP_LEFT, 2, 0);
    no_scroll(cam0_lbl);

    cam_dsc[0].header.w = CAM_FRAME_W;
    cam_dsc[0].header.h = CAM_FRAME_H;
    cam_dsc[0].header.cf = LV_COLOR_FORMAT_RGB565;
    cam_dsc[0].data_size = CAM_FRAME_W * CAM_FRAME_H * 2;
    cam_dsc[0].data = NULL;

    cam_img[0] = lv_image_create(scr_dash);
    lv_obj_set_pos(cam_img[0], 12, 56);
    lv_image_set_scale(cam_img[0], (310 * 256) / CAM_FRAME_W);  /* Scale to ~310px wide */
    no_scroll(cam_img[0]);

    /* ── Camera 1 ───────────────────────────────────────────── */
    lv_obj_t *cam1_card = lv_obj_create(scr_dash);
    lv_obj_set_size(cam1_card, 314, 260);
    lv_obj_set_pos(cam1_card, 326, 36);
    lv_obj_set_style_bg_color(cam1_card, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(cam1_card, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(cam1_card, 1, 0);
    lv_obj_set_style_radius(cam1_card, 0, 0);
    lv_obj_set_style_pad_all(cam1_card, 2, 0);
    no_scroll(cam1_card);

    lv_obj_t *cam1_lbl = lv_label_create(cam1_card);
    lv_label_set_text(cam1_lbl, LV_SYMBOL_VIDEO " Cam 1");
    lv_obj_set_style_text_color(cam1_lbl, lv_color_hex(0x00d4ff), 0);
    lv_obj_align(cam1_lbl, LV_ALIGN_TOP_LEFT, 2, 0);
    no_scroll(cam1_lbl);

    cam_dsc[1].header.w = CAM_FRAME_W;
    cam_dsc[1].header.h = CAM_FRAME_H;
    cam_dsc[1].header.cf = LV_COLOR_FORMAT_RGB565;
    cam_dsc[1].data_size = CAM_FRAME_W * CAM_FRAME_H * 2;
    cam_dsc[1].data = NULL;

    cam_img[1] = lv_image_create(scr_dash);
    lv_obj_set_pos(cam_img[1], 330, 56);
    lv_image_set_scale(cam_img[1], (310 * 256) / CAM_FRAME_W);
    no_scroll(cam_img[1]);

    /* ── FPS / Info bar ─────────────────────────────────────── */
    lbl_fps = lv_label_create(scr_dash);
    lv_label_set_text(lbl_fps, "FPS: -- | --");
    lv_obj_set_style_text_color(lbl_fps, lv_color_hex(0x888888), 0);
    lv_obj_set_width(lbl_fps, 620);
    lv_label_set_long_mode(lbl_fps, LV_LABEL_LONG_CLIP);
    opaque_label(lbl_fps, 0x0f0f23);
    lv_obj_set_pos(lbl_fps, 12, 300);

    /* ── Weather Panel (right side) ─────────────────────────── */
    lv_obj_t *weather_card = lv_obj_create(scr_dash);
    lv_obj_set_size(weather_card, 156, 440);
    lv_obj_set_pos(weather_card, 640, 36);
    lv_obj_set_style_bg_color(weather_card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(weather_card, lv_color_hex(0x0e4d92), 0);
    lv_obj_set_style_border_width(weather_card, 1, 0);
    lv_obj_set_style_radius(weather_card, 0, 0);
    lv_obj_set_style_pad_all(weather_card, 10, 0);
    no_scroll(weather_card);

    lv_obj_t *weather_title = lv_label_create(weather_card);
    lv_label_set_text(weather_title, LV_SYMBOL_CHARGE " Weather");
    lv_obj_set_style_text_color(weather_title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(weather_title, &lv_font_montserrat_16, 0);
    lv_obj_align(weather_title, LV_ALIGN_TOP_MID, 0, 0);

    lbl_city = lv_label_create(weather_card);
    lv_label_set_text(lbl_city, WEATHER_CITY);
    lv_obj_set_style_text_color(lbl_city, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(lbl_city, LV_ALIGN_TOP_MID, 0, 25);
    lv_label_set_long_mode(lbl_city, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_city, 136);
    lv_obj_set_style_text_align(lbl_city, LV_TEXT_ALIGN_CENTER, 0);

    lbl_weather_icon = lv_label_create(weather_card);
    lv_label_set_text(lbl_weather_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(lbl_weather_icon, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_text_font(lbl_weather_icon, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_weather_icon, LV_ALIGN_TOP_MID, 0, 60);

    lbl_temp = lv_label_create(weather_card);
    lv_label_set_text(lbl_temp, "--.-°C");
    lv_obj_set_style_text_color(lbl_temp, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_temp, LV_ALIGN_TOP_MID, 0, 100);

    lbl_desc = lv_label_create(weather_card);
    lv_label_set_text(lbl_desc, "Loading...");
    lv_obj_set_style_text_color(lbl_desc, lv_color_hex(0xcccccc), 0);
    lv_obj_align(lbl_desc, LV_ALIGN_TOP_MID, 0, 140);
    lv_label_set_long_mode(lbl_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_desc, 136);
    lv_obj_set_style_text_align(lbl_desc, LV_TEXT_ALIGN_CENTER, 0);

    lbl_wind = lv_label_create(weather_card);
    lv_label_set_text(lbl_wind, "Wind: -- km/h");
    lv_obj_set_style_text_color(lbl_wind, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(lbl_wind, LV_ALIGN_TOP_MID, 0, 180);

    /* ── Settings Button ────────────────────────────────────── */
    lv_obj_t *btn_settings = lv_button_create(weather_card);
    lv_obj_set_size(btn_settings, 130, 40);
    lv_obj_align(btn_settings, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x0e4d92), 0);
    lv_obj_add_event_cb(btn_settings, settings_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(btn_settings);
    lv_label_set_text(btn_lbl, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_center(btn_lbl);

    return scr_dash;
}

void ui_dashboard_update_cam(int node_idx, uint16_t *rgb_data, int w, int h)
{
    if (node_idx < 0 || node_idx >= 2 || !cam_img[node_idx]) return;
    cam_dsc[node_idx].data = (const uint8_t *)rgb_data;
    cam_dsc[node_idx].header.w = (uint32_t)w;
    cam_dsc[node_idx].header.h = (uint32_t)h;
    lv_image_set_src(cam_img[node_idx], &cam_dsc[node_idx]);
    invalidate_dashboard();
}

void ui_dashboard_update_weather(void)
{
    weather_data_t w = weather_get();
    if (!w.valid) return;

    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f°C", w.temperature);
    lv_label_set_text(lbl_temp, buf);

    lv_label_set_text(lbl_desc, w.description);

    snprintf(buf, sizeof(buf), "Wind: %.0f km/h", w.wind_speed);
    lv_label_set_text(lbl_wind, buf);

    lv_label_set_text(lbl_weather_icon, weather_code_to_icon(w.weather_code));
    invalidate_dashboard();
}

void ui_dashboard_update_status(void)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             LV_SYMBOL_WIFI "  GigaSenseHub  |  Nodes: %d  |  N0:%s N1:%s  |  Net: %s",
             g_active_node_count,
             g_nodes[0].active ? "ON" : "--",
             g_nodes[1].active ? "ON" : "--",
             wifi_sta_is_connected() ? "Online" : "AP only");
    lv_label_set_text(lbl_status, buf);
    lv_obj_set_style_text_color(lbl_status,
        g_active_node_count > 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff8800), 0);
    invalidate_dashboard();
}
