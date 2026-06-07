/**
 * @file ui_settings.cpp
 * @brief Settings screen with 8 sections: Network, Cameras, Display,
 *        Security, System Info, OTA, Language, About.
 *        Integrates NVS persistence, power management, i18n.
 */
#include "ui_settings.h"
#include "definitions.h"
#include "wifi_ap.h"
#include "stream_client.h"
#include "power_mgmt.h"
#include "settings.h"
#include "i18n.h"
#include "ui_theme.h"
#include "ui_toast.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <stdio.h>

static lv_obj_t *scr_settings = NULL;
static lv_obj_t *s_menu = NULL;

extern "C" void app_switch_to_dashboard(void);
extern "C" void app_switch_to_settings(void);

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    app_switch_to_dashboard();
}

/* Menu value-changed callback — fires on every back-button press.
   Only go to dashboard when the back button is at the root page. */
static void menu_back_cb(lv_event_t *e)
{
    lv_obj_t *menu = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *back_btn = lv_menu_get_main_header_back_button(menu);
    if (lv_menu_back_button_is_root(menu, back_btn)) {
        app_switch_to_dashboard();
    }
}

static void reboot_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "User requested reboot");
    esp_restart();
}

static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    settings_set_brightness(val);
    power_mgmt_set_brightness(val);
}

static void lang_toggle_cb(lv_event_t *e)
{
    (void)e;
    lang_t cur = i18n_get_language();
    lang_t next = (cur == LANG_EN) ? LANG_DE : LANG_EN;
    i18n_set_language(next);
    settings_set_language((int)next);
    /* Rebuild settings screen to reflect new language */
    app_switch_to_settings();
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

    lv_obj_t *back_btn = lv_button_create(top_bar);
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
    lv_obj_t *disp_page = lv_menu_page_create(menu, i18n(S_DISPLAY));
    lv_obj_set_style_pad_hor(disp_page, 20, 0);

    lv_obj_t *disp_title = lv_label_create(disp_page);
    lv_label_set_text(disp_title, i18n(S_DISPLAY));
    gsh_style_title(disp_title);

    lv_obj_t *bright_lbl = lv_label_create(disp_page);
    lv_label_set_text(bright_lbl, i18n(S_BRIGHTNESS));
    lv_obj_set_style_text_color(bright_lbl, lv_color_white(), 0);

    lv_obj_t *slider = lv_slider_create(disp_page);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, settings_get_brightness(), LV_ANIM_OFF);
    lv_obj_set_width(slider, 300);
    lv_obj_set_style_bg_color(slider, lv_color_hex(GSH_COLOR_ACCENT_DIM), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(GSH_COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    char dim_buf[64];
    snprintf(dim_buf, sizeof(dim_buf), "%s: %d %s",
             i18n(S_DIM_TIMEOUT), settings_get_dim_timeout(), i18n(S_SECONDS));
    lv_obj_t *dim_lbl = lv_label_create(disp_page);
    lv_label_set_text(dim_lbl, dim_buf);
    lv_obj_set_style_text_color(dim_lbl, lv_color_hex(GSH_COLOR_TEXT_SEC), 0);

    snprintf(dim_buf, sizeof(dim_buf), "%s: %d %s",
             i18n(S_OFF_TIMEOUT), settings_get_off_timeout(), i18n(S_SECONDS));
    lv_obj_t *off_lbl = lv_label_create(disp_page);
    lv_label_set_text(off_lbl, dim_buf);
    lv_obj_set_style_text_color(off_lbl, lv_color_hex(GSH_COLOR_TEXT_SEC), 0);

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
    lv_obj_t *about_page = lv_menu_page_create(menu, i18n(S_ABOUT));
    lv_obj_set_style_pad_hor(about_page, 20, 0);

    lv_obj_t *about_title = lv_label_create(about_page);
    lv_label_set_text(about_title, i18n(S_ABOUT));
    gsh_style_title(about_title);

    lv_obj_t *ver_lbl = lv_label_create(about_page);
    lv_label_set_text(ver_lbl,
        "GigaSenseHub v2.1\n"
        "Waveshare ESP32-S3 7\" Display\n"
        "800x480 RGB LCD\n\n"
        "Camera: 2x XIAO ESP32-S3 Sense\n"
        "Protocol: MJPEG/HTTP + HMAC-SHA256\n\n"
        "Built with ESP-IDF 5.3 + LVGL 9.2");
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(GSH_COLOR_TEXT_SEC), 0);

    lv_obj_t *btn_reboot = lv_button_create(about_page);
    lv_obj_set_size(btn_reboot, 200, 40);
    lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(0x8B0000), 0);
    lv_obj_add_event_cb(btn_reboot, reboot_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reboot_lbl = lv_label_create(btn_reboot);
    lv_label_set_text(reboot_lbl, LV_SYMBOL_REFRESH " Reboot");
    lv_obj_set_style_text_color(reboot_lbl, lv_color_white(), 0);
    lv_obj_center(reboot_lbl);

    /* ── System Info Section ──────────────────────────── */
    lv_obj_t *sysinfo_page = lv_menu_page_create(menu, i18n(S_SYSTEM_INFO));
    lv_obj_set_style_pad_hor(sysinfo_page, 20, 0);

    lv_obj_t *sys_title = lv_label_create(sysinfo_page);
    lv_label_set_text(sys_title, i18n(S_SYSTEM_INFO));
    gsh_style_title(sys_title);

    char sys_buf[256];
    int64_t up_us = esp_timer_get_time();
    int up_h = (int)(up_us / 3600000000LL);
    int up_m = (int)((up_us % 3600000000LL) / 60000000LL);
    int up_s = (int)((up_us % 60000000LL) / 1000000LL);

    wifi_ap_record_t ap_info;
    int8_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) rssi = ap_info.rssi;

    snprintf(sys_buf, sizeof(sys_buf),
        "%s: %u KB / %u KB\n"
        "%s: %02d:%02d:%02d\n"
        "%s: %d dBm\n"
        "PSRAM Free: %u KB\n"
        "Power: %s\n"
        "Tasks: %u",
        i18n(S_FREE_HEAP),
        (unsigned)(esp_get_free_heap_size() / 1024),
        (unsigned)(esp_get_minimum_free_heap_size() / 1024),
        i18n(S_UPTIME), up_h, up_m, up_s,
        i18n(S_WIFI_RSSI), rssi,
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
        power_mgmt_get_state() == POWER_STATE_ACTIVE ? "Active" :
        power_mgmt_get_state() == POWER_STATE_DIM ? "Dim" : "Off",
        (unsigned)uxTaskGetNumberOfTasks());
    lv_obj_t *sys_lbl = lv_label_create(sysinfo_page);
    lv_label_set_text(sys_lbl, sys_buf);
    lv_obj_set_style_text_color(sys_lbl, lv_color_white(), 0);

    /* ── OTA Section ──────────────────────────────────── */
    lv_obj_t *ota_page = lv_menu_page_create(menu, i18n(S_OTA_UPDATE));
    lv_obj_set_style_pad_hor(ota_page, 20, 0);

    lv_obj_t *ota_title = lv_label_create(ota_page);
    lv_label_set_text(ota_title, i18n(S_OTA_UPDATE));
    gsh_style_title(ota_title);

    lv_obj_t *ota_info = lv_label_create(ota_page);
    lv_label_set_text(ota_info,
        "Push firmware to camera nodes\n"
        "over WiFi (HTTP OTA).\n\n"
        "Node must be connected and\n"
        "accessible on the AP network.");
    lv_obj_set_style_text_color(ota_info, lv_color_white(), 0);

    for (int i = 0; i < MAX_NODES; i++) {
        char ota_buf[64];
        snprintf(ota_buf, sizeof(ota_buf), "%s %d: %s (%s)",
                 i18n(S_NODE), i,
                 g_nodes[i].active ? i18n(S_ACTIVE) : i18n(S_OFFLINE),
                 g_nodes[i].ip);
        lv_obj_t *node_lbl = lv_label_create(ota_page);
        lv_label_set_text(node_lbl, ota_buf);
        lv_obj_set_style_text_color(node_lbl,
            g_nodes[i].active ? gsh_color_success() : gsh_color_error(), 0);
    }

    /* ── Language Section ─────────────────────────────── */
    lv_obj_t *lang_page = lv_menu_page_create(menu, i18n(S_LANGUAGE));
    lv_obj_set_style_pad_hor(lang_page, 20, 0);

    lv_obj_t *lang_title = lv_label_create(lang_page);
    lv_label_set_text(lang_title, i18n(S_LANGUAGE));
    gsh_style_title(lang_title);

    lv_obj_t *cur_lang_lbl = lv_label_create(lang_page);
    lv_label_set_text(cur_lang_lbl,
        i18n_get_language() == LANG_EN ? "Current: English" : "Aktuell: Deutsch");
    lv_obj_set_style_text_color(cur_lang_lbl, lv_color_white(), 0);

    lv_obj_t *lang_btn = lv_button_create(lang_page);
    lv_obj_set_size(lang_btn, 250, 40);
    gsh_style_button(lang_btn);
    lv_obj_add_event_cb(lang_btn, lang_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lang_btn_lbl = lv_label_create(lang_btn);
    lv_label_set_text(lang_btn_lbl,
        i18n_get_language() == LANG_EN ? "Switch to Deutsch" : "Switch to English");
    lv_obj_set_style_text_color(lang_btn_lbl, lv_color_white(), 0);
    lv_obj_center(lang_btn_lbl);

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
    lv_label_set_text_fmt(about_item, LV_SYMBOL_LIST " %s", i18n(S_ABOUT));
    lv_obj_set_style_text_color(about_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, about_page);

    cont = lv_menu_cont_create(root_page);
    lv_obj_t *sys_item = lv_label_create(cont);
    lv_label_set_text_fmt(sys_item, LV_SYMBOL_CHARGE " %s", i18n(S_SYSTEM_INFO));
    lv_obj_set_style_text_color(sys_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, sysinfo_page);

    cont = lv_menu_cont_create(root_page);
    lv_obj_t *ota_item = lv_label_create(cont);
    lv_label_set_text_fmt(ota_item, LV_SYMBOL_DOWNLOAD " %s", i18n(S_OTA_UPDATE));
    lv_obj_set_style_text_color(ota_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, ota_page);

    cont = lv_menu_cont_create(root_page);
    lv_obj_t *lang_item = lv_label_create(cont);
    lv_label_set_text_fmt(lang_item, LV_SYMBOL_NEW_LINE " %s", i18n(S_LANGUAGE));
    lv_obj_set_style_text_color(lang_item, lv_color_white(), 0);
    lv_menu_set_load_page_event(menu, cont, lang_page);

    lv_menu_set_page(menu, root_page);

    return scr_settings;
}
