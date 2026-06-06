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
static lv_obj_t *s_menu = NULL;

extern "C" void app_switch_to_dashboard(void);

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    app_switch_to_dashboard();
}

/* Menu value-changed callback — fires on every back-button press.
   Only go to dashboard when the back button is at the root page. */
static void menu_back_cb(lv_event_t *e)
{
    lv_obj_t *menu = lv_event_get_target(e);
    lv_obj_t *back_btn = lv_menu_get_main_header_back_btn(menu);
    if (lv_menu_back_btn_is_root(menu, back_btn)) {
        app_switch_to_dashboard();
    }
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

    /* ── Top bar with back button ─────────────────────── */
    lv_obj_t *top_bar = lv_obj_create(scr_settings);
    lv_obj_set_size(top_bar, 800, 44);
    lv_obj_align(top_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 4, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn = lv_btn_create(top_bar);
    lv_obj_set_size(back_btn, 100, 34);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1a1a2e), 0);
    lv_obj_add_event_cb(back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x00d4ff), 0);
    lv_obj_center(back_lbl);

    lv_obj_t *title_lbl = lv_label_create(top_bar);
    lv_label_set_text(title_lbl, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, 0);

    /* ── Menu below top bar ───────────────────────────── */
    lv_obj_t *menu = lv_menu_create(scr_settings);
    lv_obj_set_size(menu, 780, 416);
    lv_obj_align(menu, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(menu, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(menu, 0, 0);

    /* LVGL menu handles sub-page back navigation internally via its
       history stack — back button appears automatically when depth >= 2.
       We do NOT enable root_back_btn since the top-bar "← Back" handles
       dashboard return. */

    /* ── Network Info Section ────────────────────────── */
    lv_obj_t *net_page = lv_menu_page_create(menu, "Network");
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

    /* ── Camera Nodes Section ─────────────────────────── */
    lv_obj_t *cam_page = lv_menu_page_create(menu, "Cameras");
    lv_obj_set_style_pad_hor(cam_page, 20, 0);

    lv_obj_t *cam_title = lv_label_create(cam_page);
    lv_label_set_text(cam_title, "Camera Nodes");
    lv_obj_set_style_text_color(cam_title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(cam_title, &lv_font_montserrat_18, 0);

    for (int i = 0; i < MAX_NODES; i++) {
        char cam_info[128];
        snprintf(cam_info, sizeof(cam_info),
                 "Node %d: %s\n  IP: %s\n  Stream: %s",
                 i, g_nodes[i].active ? "Active" : "Offline",
                 g_nodes[i].ip,
                 g_nodes[i].stream_connected ? "Connected" : "Disconnected");
        lv_obj_t *cam_lbl = lv_label_create(cam_page);
        lv_label_set_text(cam_lbl, cam_info);
        lv_obj_set_style_text_color(cam_lbl,
            g_nodes[i].active ? lv_color_hex(0x00ff88) : lv_color_hex(0xff4444), 0);
    }

    /* ── Display Section ─────────────────────────────── */
    lv_obj_t *disp_page = lv_menu_page_create(menu, "Display");
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

    /* ── Security Section ─────────────────────────────── */
    lv_obj_t *sec_page = lv_menu_page_create(menu, "Security");
    lv_obj_set_style_pad_hor(sec_page, 20, 0);

    lv_obj_t *sec_title = lv_label_create(sec_page);
    lv_label_set_text(sec_title, "Security");
    lv_obj_set_style_text_color(sec_title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(sec_title, &lv_font_montserrat_18, 0);

    lv_obj_t *sec_lbl = lv_label_create(sec_page);
    lv_label_set_text(sec_lbl,
        "Authentication: HMAC-SHA256\n"
        "PIN Lock: Enabled\n"
        "OTA: Available per-node");
    lv_obj_set_style_text_color(sec_lbl, lv_color_white(), 0);

    /* ── About Section ───────────────────────────────── */
    lv_obj_t *about_page = lv_menu_page_create(menu, "About");
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
    lv_obj_t *cam_item = lv_label_create(cont);
    lv_label_set_text(cam_item, LV_SYMBOL_VIDEO " Cameras");
    lv_obj_set_style_text_color(cam_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, cam_page);

    cont = lv_menu_cont_create(root_page);
    lv_obj_t *disp_item = lv_label_create(cont);
    lv_label_set_text(disp_item, LV_SYMBOL_IMAGE " Display");
    lv_obj_set_style_text_color(disp_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, disp_page);

    cont = lv_menu_cont_create(root_page);
    lv_obj_t *sec_item = lv_label_create(cont);
    lv_label_set_text(sec_item, LV_SYMBOL_EYE_OPEN " Security");
    lv_obj_set_style_text_color(sec_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, sec_page);

    cont = lv_menu_cont_create(root_page);
    lv_obj_t *about_item = lv_label_create(cont);
    lv_label_set_text(about_item, LV_SYMBOL_LIST " About");
    lv_obj_set_style_text_color(about_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, about_page);

    lv_menu_set_page(menu, root_page);

    return scr_settings;
}
