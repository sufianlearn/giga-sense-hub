/**
 * @file ui_settings.cpp
 * @brief Settings screen — ported from logic_suite reference.
 *        Includes: Display brightness, WiFi AP info, About section.
 */
#include "ui_settings.h"
#include "definitions.h"
#include "wifi_ap.h"
#include "stream_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>

static lv_obj_t *scr_settings = NULL;

extern "C" void app_switch_to_dashboard(void);

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    app_switch_to_dashboard();
}

static void reboot_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "User requested reboot");
    esp_restart();
}

lv_obj_t *ui_settings_create(void)
{
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, lv_color_hex(0x0f0f23), 0);
    lv_obj_clear_flag(scr_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scr_settings, LV_DIR_NONE);

    /* ── Menu ─────────────────────────────────────────── */
    lv_obj_t *menu = lv_menu_create(scr_settings);
    lv_obj_set_size(menu, 780, 460);
    lv_obj_center(menu);
    lv_obj_set_style_bg_color(menu, lv_color_hex(0x16213e), 0);

    /* Back button */
    lv_obj_t *back_btn = lv_menu_get_main_header_back_btn(menu);
    lv_obj_add_event_cb(back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);

    /* ── Network Info Section ────────────────────────── */
    lv_obj_t *net_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(net_page, 20, 0);

    lv_obj_t *net_title = lv_label_create(net_page);
    lv_label_set_text(net_title, "Network");
    lv_obj_set_style_text_color(net_title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(net_title, &lv_font_montserrat_18, 0);

    lv_obj_t *ssid_lbl = lv_label_create(net_page);
    char info[128];
    snprintf(info, sizeof(info),
             "Mode: Access Point\n"
             "SSID: %s\n"
             "IP: %s\n"
             "Channel: %d\n"
             "Nodes: %d connected",
             AP_SSID_WS, AP_IP, AP_CHANNEL_WS, g_active_node_count);
    lv_label_set_text(ssid_lbl, info);
    lv_obj_set_style_text_color(ssid_lbl, lv_color_white(), 0);

    /* ── Display Section ─────────────────────────────── */
    lv_obj_t *disp_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(disp_page, 20, 0);

    lv_obj_t *disp_title = lv_label_create(disp_page);
    lv_label_set_text(disp_title, "Display");
    lv_obj_set_style_text_color(disp_title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(disp_title, &lv_font_montserrat_18, 0);

    lv_obj_t *bright_lbl = lv_label_create(disp_page);
    lv_label_set_text(bright_lbl, "Brightness");
    lv_obj_set_style_text_color(bright_lbl, lv_color_white(), 0);

    lv_obj_t *slider = lv_slider_create(disp_page);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, 80, LV_ANIM_OFF);
    lv_obj_set_width(slider, 300);

    /* ── About Section ───────────────────────────────── */
    lv_obj_t *about_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(about_page, 20, 0);

    lv_obj_t *about_title = lv_label_create(about_page);
    lv_label_set_text(about_title, "About");
    lv_obj_set_style_text_color(about_title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(about_title, &lv_font_montserrat_18, 0);

    lv_obj_t *ver_lbl = lv_label_create(about_page);
    lv_label_set_text(ver_lbl,
        "GigaSenseHub v2.0\n"
        "Waveshare ESP32-S3 7\" Display\n"
        "800x480 RGB LCD\n\n"
        "Camera: 2x XIAO ESP32-S3 Sense\n"
        "Protocol: MJPEG/HTTP + HMAC-SHA256\n\n"
        "Built with ESP-IDF + LVGL 8.3");
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0xcccccc), 0);

    /* Reboot button */
    lv_obj_t *btn_reboot = lv_btn_create(about_page);
    lv_obj_set_size(btn_reboot, 200, 40);
    lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(0x8B0000), 0);
    lv_obj_add_event_cb(btn_reboot, reboot_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reboot_lbl = lv_label_create(btn_reboot);
    lv_label_set_text(reboot_lbl, LV_SYMBOL_REFRESH " Reboot");
    lv_obj_set_style_text_color(reboot_lbl, lv_color_white(), 0);
    lv_obj_center(reboot_lbl);

    /* ── Menu sidebar items ──────────────────────────── */
    lv_obj_t *root_page = lv_menu_page_create(menu, "Settings");

    lv_obj_t *cont;

    cont = lv_menu_cont_create(root_page);
    lv_obj_t *net_item = lv_label_create(cont);
    lv_label_set_text(net_item, LV_SYMBOL_WIFI " Network");
    lv_obj_set_style_text_color(net_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, net_page);

    cont = lv_menu_cont_create(root_page);
    lv_obj_t *disp_item = lv_label_create(cont);
    lv_label_set_text(disp_item, LV_SYMBOL_IMAGE " Display");
    lv_obj_set_style_text_color(disp_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, disp_page);

    cont = lv_menu_cont_create(root_page);
    lv_obj_t *about_item = lv_label_create(cont);
    lv_label_set_text(about_item, LV_SYMBOL_LIST " About");
    lv_obj_set_style_text_color(about_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, about_page);

    lv_menu_set_page(menu, root_page);
    lv_menu_set_sidebar_page(menu, root_page);

    return scr_settings;
}
