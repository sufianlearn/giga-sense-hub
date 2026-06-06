/**
 * @file ui_login.cpp
 * @brief PIN login screen — same style as logic_suite reference.
 */
#include "ui_login.h"
#include "definitions.h"
#include "esp_log.h"
#include <string.h>

static lv_obj_t *scr_login = NULL;
static lv_obj_t *ta_pin    = NULL;
static const char *CORRECT_PIN = "1234";

/* Forward declare — set by main */
extern "C" void app_switch_to_dashboard(void);

static void pin_check_cb(lv_event_t *e)
{
    const char *txt = lv_textarea_get_text(ta_pin);
    if (strcmp(txt, CORRECT_PIN) == 0) {
        ESP_LOGI(TAG, "PIN correct — switching to dashboard");
        app_switch_to_dashboard();
    } else if (strlen(txt) >= 4) {
        lv_textarea_set_text(ta_pin, "");
        /* Flash red briefly */
        lv_obj_set_style_border_color(ta_pin, lv_color_hex(0xFF0000), 0);
        /* Reset after 500ms via timer */
    }
}

static void kb_event_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        pin_check_cb(e);
    }
}

lv_obj_t *ui_login_create(void)
{
    scr_login = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_login, lv_color_hex(0x1a1a2e), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr_login);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN "  GigaSenseHub");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(scr_login);
    lv_label_set_text(sub, "Camera Surveillance System");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 100);

    /* PIN label */
    lv_obj_t *lbl = lv_label_create(scr_login);
    lv_label_set_text(lbl, "Enter PIN:");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 150);

    /* PIN input */
    ta_pin = lv_textarea_create(scr_login);
    lv_textarea_set_max_length(ta_pin, 4);
    lv_textarea_set_password_mode(ta_pin, true);
    lv_textarea_set_one_line(ta_pin, true);
    lv_textarea_set_placeholder_text(ta_pin, "****");
    lv_obj_set_width(ta_pin, 200);
    lv_obj_align(ta_pin, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_set_style_bg_color(ta_pin, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_text_color(ta_pin, lv_color_white(), 0);
    lv_obj_set_style_border_color(ta_pin, lv_color_hex(0x00d4ff), 0);

    /* Numpad keyboard */
    lv_obj_t *kb = lv_keyboard_create(scr_login);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(kb, ta_pin);
    lv_obj_set_size(kb, 400, 200);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_text_color(kb, lv_color_white(), 0);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, NULL);

    return scr_login;
}
