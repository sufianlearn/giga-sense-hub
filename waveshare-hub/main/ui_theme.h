#pragma once

/**
 * @file ui_theme.h
 * @brief GigaSenseHub brand theme — consistent colors, spacing, helpers.
 *
 *        Design language:
 *        - Dark background (#0f0f23) with navy cards (#16213e)
 *        - Cyan accent (#00d4ff) for interactive elements
 *        - Green/amber/red for status indicators
 */
#include "lvgl.h"

/* ── Brand colors ─────────────────────────────────────────── */
#define GSH_COLOR_BG_DARK     0x0f0f23
#define GSH_COLOR_BG_CARD     0x16213e
#define GSH_COLOR_BG_ELEVATED 0x1a1a2e
#define GSH_COLOR_ACCENT      0x00d4ff
#define GSH_COLOR_ACCENT_DIM  0x0e4d92
#define GSH_COLOR_TEXT        0xFFFFFF
#define GSH_COLOR_TEXT_SEC    0xaaaaaa
#define GSH_COLOR_TEXT_DIM    0x888888
#define GSH_COLOR_SUCCESS     0x00ff88
#define GSH_COLOR_WARNING     0xff8800
#define GSH_COLOR_ERROR       0xff4444
#define GSH_COLOR_GOLD        0xFFD700

/* ── Semantic helpers (inline for zero overhead) ──────────── */
static inline lv_color_t gsh_color_bg(void)      { return lv_color_hex(GSH_COLOR_BG_DARK); }
static inline lv_color_t gsh_color_card(void)     { return lv_color_hex(GSH_COLOR_BG_CARD); }
static inline lv_color_t gsh_color_accent(void)   { return lv_color_hex(GSH_COLOR_ACCENT); }
static inline lv_color_t gsh_color_text(void)     { return lv_color_hex(GSH_COLOR_TEXT); }
static inline lv_color_t gsh_color_text_sec(void) { return lv_color_hex(GSH_COLOR_TEXT_SEC); }
static inline lv_color_t gsh_color_success(void)  { return lv_color_hex(GSH_COLOR_SUCCESS); }
static inline lv_color_t gsh_color_warning(void)  { return lv_color_hex(GSH_COLOR_WARNING); }
static inline lv_color_t gsh_color_error(void)    { return lv_color_hex(GSH_COLOR_ERROR); }

/* ── Widget style helpers ─────────────────────────────────── */
static inline void gsh_style_card(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, gsh_color_card(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(GSH_COLOR_ACCENT_DIM), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 4, 0);
    lv_obj_set_style_pad_all(obj, 10, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static inline void gsh_style_title(lv_obj_t *lbl)
{
    lv_obj_set_style_text_color(lbl, gsh_color_accent(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
}

static inline void gsh_style_body(lv_obj_t *lbl)
{
    lv_obj_set_style_text_color(lbl, gsh_color_text(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
}

static inline void gsh_style_button(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(GSH_COLOR_ACCENT_DIM), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 8, 0);
}

static inline void gsh_no_scroll(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}
