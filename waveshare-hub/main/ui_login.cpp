/**
 * @file ui_login.cpp
 * @brief Fixed PIN login screen for Waveshare 800x480.
 *
 * Avoid lv_keyboard/lv_textarea on this board: when GT911 reports noisy
 * drag coordinates, LVGL keyboard/text-area widgets can scroll the screen.
 * This login uses fixed buttons only and clears scroll on every container.
 */
#include "ui_login.h"
#include "definitions.h"
#include "esp_log.h"
#include <string.h>

static lv_obj_t *scr_login = NULL;
static lv_obj_t *lbl_pin   = NULL;
static char s_pin[5]       = {0};
static uint8_t s_pin_len   = 0;
static const char *CORRECT_PIN = "1234";

extern "C" void app_switch_to_dashboard(void);

static void no_scroll(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void update_pin_label(void)
{
    char dots[5] = "____";
    for (uint8_t i = 0; i < s_pin_len && i < 4; i++) dots[i] = '*';
    lv_label_set_text(lbl_pin, dots);
}

static void reset_pin(void)
{
    memset(s_pin, 0, sizeof(s_pin));
    s_pin_len = 0;
    update_pin_label();
}

static void digit_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *digit = (const char *)lv_event_get_user_data(e);
    if (!digit || s_pin_len >= 4) return;

    s_pin[s_pin_len++] = digit[0];
    s_pin[s_pin_len] = '\0';
    update_pin_label();

    if (s_pin_len == 4) {
        if (strcmp(s_pin, CORRECT_PIN) == 0) {
            ESP_LOGI(TAG, "PIN correct — switching to dashboard");
            app_switch_to_dashboard();
        } else {
            ESP_LOGW(TAG, "PIN incorrect");
            lv_obj_set_style_text_color(lbl_pin, lv_color_hex(0xff4444), 0);
            reset_pin();
            lv_obj_set_style_text_color(lbl_pin, lv_color_white(), 0);
        }
    }
}

static void clear_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    reset_pin();
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, int x, int y, lv_event_cb_t cb, const char *userdata)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 90, 56);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    no_scroll(btn);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)userdata);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl);
    no_scroll(lbl);
    return btn;
}

lv_obj_t *ui_login_create(void)
{
    scr_login = lv_obj_create(NULL);
    lv_obj_set_size(scr_login, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(scr_login, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_pad_all(scr_login, 0, 0);
    no_scroll(scr_login);

    lv_obj_t *title = lv_label_create(scr_login);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN "  GigaSenseHub");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);
    no_scroll(title);

    lv_obj_t *sub = lv_label_create(scr_login);
    lv_label_set_text(sub, "Camera Surveillance System");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 75);
    no_scroll(sub);

    lv_obj_t *pin_box = lv_obj_create(scr_login);
    lv_obj_set_size(pin_box, 220, 60);
    lv_obj_align(pin_box, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_set_style_bg_color(pin_box, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(pin_box, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(pin_box, 2, 0);
    lv_obj_set_style_radius(pin_box, 8, 0);
    no_scroll(pin_box);

    lbl_pin = lv_label_create(pin_box);
    lv_obj_set_style_text_color(lbl_pin, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_pin, &lv_font_montserrat_28, 0);
    lv_obj_center(lbl_pin);
    no_scroll(lbl_pin);
    reset_pin();

    /* Fixed numpad: 3 columns x 4 rows, entirely inside 800x480. */
    const int start_x = 265;
    const int start_y = 205;
    const int gap_x = 105;
    const int gap_y = 65;
    make_btn(scr_login, "1", start_x + 0*gap_x, start_y + 0*gap_y, digit_btn_cb, "1");
    make_btn(scr_login, "2", start_x + 1*gap_x, start_y + 0*gap_y, digit_btn_cb, "2");
    make_btn(scr_login, "3", start_x + 2*gap_x, start_y + 0*gap_y, digit_btn_cb, "3");
    make_btn(scr_login, "4", start_x + 0*gap_x, start_y + 1*gap_y, digit_btn_cb, "4");
    make_btn(scr_login, "5", start_x + 1*gap_x, start_y + 1*gap_y, digit_btn_cb, "5");
    make_btn(scr_login, "6", start_x + 2*gap_x, start_y + 1*gap_y, digit_btn_cb, "6");
    make_btn(scr_login, "7", start_x + 0*gap_x, start_y + 2*gap_y, digit_btn_cb, "7");
    make_btn(scr_login, "8", start_x + 1*gap_x, start_y + 2*gap_y, digit_btn_cb, "8");
    make_btn(scr_login, "9", start_x + 2*gap_x, start_y + 2*gap_y, digit_btn_cb, "9");
    make_btn(scr_login, "CLR", start_x + 0*gap_x, start_y + 3*gap_y, clear_btn_cb, NULL);
    make_btn(scr_login, "0", start_x + 1*gap_x, start_y + 3*gap_y, digit_btn_cb, "0");

    return scr_login;
}
