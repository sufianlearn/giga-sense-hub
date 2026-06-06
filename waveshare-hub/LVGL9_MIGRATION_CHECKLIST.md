# LVGL 8 → 9 Migration Checklist — GigaSenseHub

Generated: 2026-06-07
Current: LVGL 8.3.x | Target: LVGL 9.x

---

## FILE: lv_conf.h

| LVGL 8 API / Macro | Status | LVGL 9 Equivalent |
|---|---|---|
| `LV_COLOR_DEPTH 16` | CHANGED | Still exists but check LV_COLOR_FORMAT usage |
| `LV_COLOR_16_SWAP` | REMOVED | Use `LV_COLOR_FORMAT_RGB565` or `_NATIVE_REVERSED` at display level |
| `LV_MEM_CUSTOM` | CHANGED | Now `LV_USE_STDLIB_MALLOC` / `LV_STDLIB_CLIB` |
| `LV_MEM_CUSTOM_INCLUDE/ALLOC/FREE/REALLOC` | REMOVED | Replaced by `LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB` |
| `LV_DISP_DEF_REFR_PERIOD` | RENAMED | `LV_DEF_REFR_PERIOD` |
| `LV_INDEV_DEF_READ_PERIOD` | RENAMED | `LV_DEF_INDEV_READ_PERIOD` |
| `LV_USE_PERF_MONITOR` | MOVED | Now `LV_USE_SYSMON` + `LV_USE_PERF_MONITOR` |
| `LV_FONT_MONTSERRAT_*` | OK | Same in v9 |
| `LV_FONT_DEFAULT` | OK | Same in v9 |
| `LV_USE_IMG` | RENAMED | `LV_USE_IMAGE` |
| `LV_USE_BTN` | OK | Same (lv_button internally but macro stays) |
| `LV_USE_COLORWHEEL` | RENAMED | `LV_USE_COLORWHEEL` removed; use `lv_colorwheel` if re-added |
| `LV_USE_IMGBTN` | RENAMED | `LV_USE_IMAGEBUTTON` |
| `LV_USE_THEME_DEFAULT` | CHANGED | Config structure differs in v9 |
| `LV_THEME_DEFAULT_DARK` | REMOVED | Theme API changed; set dark via `lv_theme_default_init()` params |

---

## FILE: lvgl_port.cpp

| Line | LVGL 8 API | Status | LVGL 9 Equivalent |
|---|---|---|---|
| 32 | `lv_disp_draw_buf_t` | REMOVED | No equivalent — draw buffers set via `lv_display_set_buffers()` |
| 33 | `lv_disp_drv_t` | REMOVED | Use `lv_display_t *` from `lv_display_create()` |
| 50 | `lv_tick_inc()` | CHANGED | Use `lv_tick_set_cb(my_tick_cb)` instead of periodic inc |
| 55 | `lvgl_flush_cb(lv_disp_drv_t *drv, ...)` | CHANGED | Signature: `void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)` |
| 56 | `lv_color_t *color_map` param | CHANGED | Now `uint8_t *px_map` |
| 59 | `drv->user_data` | CHANGED | `lv_display_get_user_data(disp)` |
| 71 | `lv_disp_flush_ready(drv)` | RENAMED | `lv_display_flush_ready(disp)` |
| 74 | `lv_indev_drv_t` | REMOVED | Use `lv_indev_t *` from `lv_indev_create()` |
| 74 | `lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)` | CHANGED | Signature: `void read_cb(lv_indev_t *indev, lv_indev_data_t *data)` |
| 77 | `drv->user_data` | CHANGED | `lv_indev_get_user_data(indev)` |
| 88 | `LV_INDEV_STATE_PR` | RENAMED | `LV_INDEV_STATE_PRESSED` |
| 90 | `LV_INDEV_STATE_REL` | RENAMED | `LV_INDEV_STATE_RELEASED` |
| 119 | `lv_timer_handler()` | OK | Same in v9 |
| 272 | `lv_init()` | OK | Same in v9 |
| 275 | `lv_color_t` (for sizeof) | CHANGED | In v9, use pixel format size; `lv_color_t` is now always 32-bit |
| 277 | `lv_disp_draw_buf_init()` | REMOVED | Use `lv_display_set_buffers(disp, buf1, NULL, size, LV_DISPLAY_RENDER_MODE_PARTIAL)` |
| 280 | `lv_disp_drv_init()` | REMOVED | Use `lv_display_create(hor, ver)` |
| 281-285 | `s_disp_drv.hor_res/ver_res/flush_cb/draw_buf/user_data` | REMOVED | Set via `lv_display_set_flush_cb()`, `lv_display_set_buffers()`, `lv_display_set_user_data()` |
| 286 | `lv_disp_drv_register()` | REMOVED | `lv_display_create()` returns the display directly |
| 288-289 | `lv_indev_drv_t` + `lv_indev_drv_init()` | REMOVED | Use `lv_indev_create()` |
| 290 | `indev_drv.type = LV_INDEV_TYPE_POINTER` | CHANGED | `lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER)` |
| 291 | `indev_drv.disp = disp` | CHANGED | `lv_indev_set_display(indev, disp)` |
| 292 | `indev_drv.read_cb` | CHANGED | `lv_indev_set_read_cb(indev, read_cb)` |
| 293 | `indev_drv.user_data` | CHANGED | `lv_indev_set_user_data(indev, tp)` |
| 294 | `lv_indev_drv_register()` | REMOVED | `lv_indev_create()` returns indev directly |

---

## FILE: main.cpp

| Line | LVGL 8 API | Status | LVGL 9 Equivalent |
|---|---|---|---|
| 30-32 | `lv_obj_t *` | OK | Same in v9 |
| 51 | `lv_scr_load(scr_dashboard)` | RENAMED | `lv_screen_load(scr_dashboard)` |
| 52 | `lv_scr_act()` | RENAMED | `lv_screen_active()` |
| 52 | `lv_obj_invalidate()` | OK | Same in v9 |
| 84 | `lv_obj_del(scr_settings)` | RENAMED | `lv_obj_delete(scr_settings)` |
| 88 | `lv_scr_load(scr_settings)` | RENAMED | `lv_screen_load(scr_settings)` |
| 225 | `lv_scr_load(scr_login)` | RENAMED | `lv_screen_load(scr_login)` |

---

## FILE: ui_login.cpp

| Line | LVGL 8 API | Status | LVGL 9 Equivalent |
|---|---|---|---|
| 14 | `lv_obj_t *` | OK | Same |
| 24-28 | `lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE)` | OK | Same flag name in v9 |
| 25 | `LV_OBJ_FLAG_SCROLL_CHAIN_HOR` | OK | Same in v9 |
| 26 | `LV_OBJ_FLAG_SCROLL_CHAIN_VER` | OK | Same in v9 |
| 27 | `LV_OBJ_FLAG_SCROLL_ELASTIC` | OK | Same in v9 |
| 28 | `LV_OBJ_FLAG_SCROLL_MOMENTUM` | OK | Same in v9 |
| 29 | `lv_obj_set_scroll_dir(obj, LV_DIR_NONE)` | OK | Same in v9 |
| 30 | `lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF)` | OK | Same in v9 |
| 37 | `lv_label_set_text()` | OK | Same in v9 |
| 49 | `lv_event_get_code(e)` | OK | Same in v9 |
| 49 | `LV_EVENT_CLICKED` | OK | Same in v9 |
| 50 | `lv_event_get_user_data(e)` | OK | Same in v9 |
| 63 | `lv_obj_set_style_text_color()` | OK | Same in v9 |
| 63 | `lv_color_hex()` | OK | Same in v9 |
| 65 | `lv_color_white()` | OK | Same in v9 |
| 78 | `lv_btn_create(parent)` | RENAMED | `lv_button_create(parent)` |
| 79 | `lv_obj_set_size()` | OK | Same in v9 |
| 80 | `lv_obj_set_pos()` | OK | Same in v9 |
| 81 | `lv_obj_set_style_bg_color()` | OK | Same in v9 |
| 82 | `lv_obj_set_style_border_color()` | OK | Same in v9 |
| 83 | `lv_obj_set_style_border_width()` | OK | Same in v9 |
| 85 | `lv_obj_add_event_cb()` | OK | Same in v9 |
| 87 | `lv_label_create()` | OK | Same in v9 |
| 90 | `lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0)` | OK | Same in v9 |
| 91 | `lv_obj_center()` | OK | Same in v9 |
| 98 | `lv_obj_create(NULL)` | OK | Same (creates screen) |
| 101 | `lv_obj_set_style_pad_all()` | OK | Same in v9 |
| 105 | `LV_SYMBOL_EYE_OPEN` | OK | Same in v9 |
| 108 | `lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35)` | OK | Same in v9 |
| 123 | `lv_obj_set_style_radius()` | OK | Same in v9 |

---

## FILE: ui_dashboard.cpp

| Line | LVGL 8 API | Status | LVGL 9 Equivalent |
|---|---|---|---|
| 30 | `lv_img_dsc_t cam_dsc[2]` | RENAMED | `lv_image_dsc_t` |
| 64 | `lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0)` | OK | Same function; `LV_OPA_COVER` unchanged |
| 72 | `lv_obj_invalidate()` | OK | Same in v9 |
| 83 | `lv_obj_create(NULL)` | OK | Same |
| 86 | `lv_obj_set_style_bg_opa()` | OK | Same |
| 101 | `LV_SYMBOL_WIFI` | OK | Same in v9 |
| 104 | `lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_CLIP)` | RENAMED | `LV_LABEL_LONG_CLIP` still exists in v9 (was briefly `LV_LABEL_LONG_MODE_CLIP` in early v9 betas but settled on same) |
| 121 | `LV_SYMBOL_VIDEO` | OK | Same in v9 |
| 126-131 | `cam_dsc[0].header.always_zero` | REMOVED | v9 `lv_image_dsc_t` header has no `always_zero` field |
| 127-128 | `cam_dsc[0].header.w / .h` | CHANGED | In v9: `header.w` and `header.h` are `int32_t` (was `uint32_t` bitfield) |
| 129 | `cam_dsc[0].header.cf = LV_IMG_CF_TRUE_COLOR` | CHANGED | `header.cf = LV_COLOR_FORMAT_RGB565` |
| 133 | `lv_img_create(scr_dash)` | RENAMED | `lv_image_create(scr_dash)` |
| 135 | `lv_img_set_zoom()` | RENAMED | `lv_image_set_scale()` — note: 256 = 100% in v8, in v9 it's also 256=100% |
| 162 | `lv_img_create()` | RENAMED | `lv_image_create()` |
| 164 | `lv_img_set_zoom()` | RENAMED | `lv_image_set_scale()` |
| 172 | `LV_LABEL_LONG_CLIP` | OK | Same |
| 188 | `LV_SYMBOL_CHARGE` | OK | Same in v9 |
| 190 | `&lv_font_montserrat_16` | OK | Same |
| 197 | `LV_LABEL_LONG_WRAP` | OK | Same in v9 |
| 199 | `lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0)` | OK | Same in v9 |
| 202 | `LV_SYMBOL_CHARGE` | OK | Same |
| 227 | `lv_btn_create()` | RENAMED | `lv_button_create()` |
| 234 | `LV_SYMBOL_SETTINGS` | OK | Same in v9 |
| 247 | `lv_img_set_src(cam_img, &cam_dsc)` | RENAMED | `lv_image_set_src()` |

---

## FILE: ui_settings.cpp

| Line | LVGL 8 API | Status | LVGL 9 Equivalent |
|---|---|---|---|
| 29 | `lv_event_get_target(e)` | OK | Same in v9 |
| 30 | `lv_menu_get_main_header_back_btn(menu)` | CHANGED | Menu widget heavily reworked in v9; check `lv_menu.h` for v9 API |
| 31 | `lv_menu_back_btn_is_root(menu, back_btn)` | CHANGED | Same — menu API rework in v9 |
| 47 | `lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE)` | OK | Same |
| 48 | `lv_obj_set_scroll_dir(scr, LV_DIR_NONE)` | OK | Same |
| 60 | `lv_btn_create(top_bar)` | RENAMED | `lv_button_create(top_bar)` |
| 67 | `LV_SYMBOL_LEFT` | OK | Same in v9 |
| 72 | `LV_SYMBOL_SETTINGS` | OK | Same in v9 |
| 78 | `lv_menu_create(scr_settings)` | OK | Same in v9 (menu widget kept) |
| 90 | `lv_menu_page_create(menu, "Network")` | OK | Same in v9 |
| 91 | `lv_obj_set_style_pad_hor()` | OK | Same in v9 |
| 145 | `lv_slider_create(disp_page)` | OK | Same in v9 |
| 146 | `lv_slider_set_range()` | OK | Same in v9 |
| 147 | `lv_slider_set_value(slider, 80, LV_ANIM_OFF)` | OK | Same; `LV_ANIM_OFF` unchanged |
| 186 | `lv_btn_create()` | RENAMED | `lv_button_create()` |
| 192 | `LV_SYMBOL_REFRESH` | OK | Same in v9 |
| 201 | `lv_menu_cont_create(root_page)` | OK | Same in v9 |
| 203 | `LV_SYMBOL_WIFI` | OK | Same |
| 205 | `lv_menu_set_load_page_event()` | OK | Same in v9 |
| 209 | `LV_SYMBOL_VIDEO` | OK | Same |
| 215 | `LV_SYMBOL_IMAGE` | OK | Same |
| 221 | `LV_SYMBOL_EYE_OPEN` | OK | Same |
| 227 | `LV_SYMBOL_LIST` | OK | Same |
| 231 | `lv_menu_set_page(menu, root_page)` | OK | Same in v9 |

---

## FILE: weather.cpp

| Line | LVGL 8 API | Status | LVGL 9 Equivalent |
|---|---|---|---|
| 51 | `LV_SYMBOL_CHARGE` | OK | Same in v9 |
| 52 | `LV_SYMBOL_IMAGE` | OK | Same in v9 |
| 53 | `LV_SYMBOL_EYE_CLOSE` | OK | Same in v9 |
| 54-55 | `LV_SYMBOL_DOWN` | OK | Same in v9 |
| 56 | `LV_SYMBOL_WARNING` | OK | Same in v9 |
| 57 | `LV_SYMBOL_DUMMY` | OK | Same in v9 |

No breaking changes in weather.cpp — only uses LV_SYMBOL_* constants.

---

## FILE: jpeg_decoder.cpp

No LVGL API calls. This file uses only TJpgDec ROM APIs and FreeRTOS.
No migration action needed.

---

## CRITICAL MIGRATION SUMMARY

### Must-Fix (will not compile with LVGL 9):

1. **lvgl_port.cpp** — Complete rewrite of display/indev init:
   - Remove `lv_disp_draw_buf_t`, `lv_disp_drv_t`, `lv_indev_drv_t`
   - Replace with `lv_display_create()` + setter functions
   - Change flush_cb signature: `(lv_display_t*, const lv_area_t*, uint8_t*)`
   - Change touch read_cb signature: `(lv_indev_t*, lv_indev_data_t*)`
   - Replace `lv_tick_inc()` with `lv_tick_set_cb()`
   - Replace `lv_disp_flush_ready()` with `lv_display_flush_ready()`

2. **ui_dashboard.cpp** — Image widget rename:
   - `lv_img_dsc_t` → `lv_image_dsc_t`
   - `lv_img_create()` → `lv_image_create()`
   - `lv_img_set_src()` → `lv_image_set_src()`
   - `lv_img_set_zoom()` → `lv_image_set_scale()`
   - `LV_IMG_CF_TRUE_COLOR` → `LV_COLOR_FORMAT_RGB565`
   - Remove `header.always_zero` field

3. **main.cpp** — Screen loading:
   - `lv_scr_load()` → `lv_screen_load()`
   - `lv_scr_act()` → `lv_screen_active()`
   - `lv_obj_del()` → `lv_obj_delete()`

4. **ui_login.cpp + ui_dashboard.cpp + ui_settings.cpp** — Button widget:
   - `lv_btn_create()` → `lv_button_create()` (6 occurrences total)

5. **lv_conf.h** — Config overhaul:
   - `LV_MEM_CUSTOM*` → `LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB`
   - `LV_COLOR_16_SWAP` → removed (handle at display level)
   - `LV_USE_IMG` → `LV_USE_IMAGE`
   - `LV_DISP_DEF_REFR_PERIOD` → `LV_DEF_REFR_PERIOD`
   - `LV_INDEV_DEF_READ_PERIOD` → `LV_DEF_INDEV_READ_PERIOD`

### Safe (no changes needed):
- All `lv_obj_set_style_*()` calls — same API in v9
- All `lv_obj_clear_flag()` / `lv_obj_add_flag()` calls — same flags
- All `LV_SYMBOL_*` constants — unchanged
- `lv_label_*` functions — same API
- `lv_slider_*` functions — same API
- `lv_menu_*` functions — mostly same (verify header back-btn APIs)
- `lv_obj_align()`, `lv_obj_center()`, `lv_obj_set_size/pos()` — same
- `LV_OPA_COVER`, `LV_ANIM_OFF`, `LV_DIR_NONE` — same
- All `lv_font_montserrat_*` references — same
- `LV_SCROLLBAR_MODE_OFF`, scroll flags — same
- `LV_LABEL_LONG_CLIP`, `LV_LABEL_LONG_WRAP` — same
- `LV_INDEV_TYPE_POINTER` — same (but set via function, not struct field)
