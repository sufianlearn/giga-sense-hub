/**
 * @file ui_toast.cpp
 * @brief Toast notification overlay — slides up from the bottom,
 *        auto-dismisses after timeout. Supports 4 severity levels
 *        with distinct colors.
 */
#include "ui_toast.h"
#include "definitions.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static lv_obj_t *s_toast = NULL;
static lv_timer_t *s_timer = NULL;

static void toast_dismiss_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_toast) {
        /* Fade out animation */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_toast);
        lv_anim_set_values(&a, lv_obj_get_style_opa(s_toast, 0), LV_OPA_TRANSP);
        lv_anim_set_time(&a, 300);
        lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
            lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
            if (v == LV_OPA_TRANSP) {
                lv_obj_delete((lv_obj_t *)obj);
            }
        });
        lv_anim_set_deleted_cb(&a, [](lv_anim_t *) {
            s_toast = NULL;
        });
        lv_anim_start(&a);
    }
    s_timer = NULL;
}

void ui_toast_init(void)
{
    s_toast = NULL;
    s_timer = NULL;
}

void ui_toast_show(const char *msg, toast_level_t level, uint32_t duration_ms)
{
    if (!msg) return;
    if (duration_ms == 0) duration_ms = 3000;

    /* Remove existing toast */
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    /* Color by severity */
    uint32_t bg_color, text_color;
    const char *icon;
    switch (level) {
    case TOAST_SUCCESS:
        bg_color = 0x1B5E20;
        text_color = 0x81C784;
        icon = LV_SYMBOL_OK;
        break;
    case TOAST_WARNING:
        bg_color = 0x4E3524;
        text_color = 0xFFB74D;
        icon = LV_SYMBOL_WARNING;
        break;
    case TOAST_ERROR:
        bg_color = 0x4A1515;
        text_color = 0xEF5350;
        icon = LV_SYMBOL_CLOSE;
        break;
    default: /* INFO */
        bg_color = 0x0D47A1;
        text_color = 0x64B5F6;
        icon = LV_SYMBOL_OK;
        break;
    }

    /* Create toast on active screen */
    lv_obj_t *scr = lv_screen_active();
    s_toast = lv_obj_create(scr);
    lv_obj_set_size(s_toast, 500, 44);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_toast, 8, 0);
    lv_obj_set_style_border_width(s_toast, 0, 0);
    lv_obj_set_style_pad_all(s_toast, 8, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(s_toast, LV_OPA_COVER, 0);

    /* Icon + message */
    char buf[256];
    snprintf(buf, sizeof(buf), "%s  %s", icon, msg);
    lv_obj_t *lbl = lv_label_create(s_toast);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_hex(text_color), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl, 480);

    /* Slide-up entrance animation */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_toast);
    lv_anim_set_values(&a, 50, -10);
    lv_anim_set_time(&a, 250);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_obj_align((lv_obj_t *)obj, LV_ALIGN_BOTTOM_MID, 0, v);
    });
    lv_anim_start(&a);

    /* Auto-dismiss timer */
    s_timer = lv_timer_create(toast_dismiss_cb, duration_ms, NULL);
    lv_timer_set_repeat_count(s_timer, 1);
}
