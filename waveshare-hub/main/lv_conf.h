/**
 * @file lv_conf.h
 * @brief LVGL 9.2 configuration for Waveshare 7" ESP32-S3
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Color */
#define LV_COLOR_DEPTH          16

/* Memory — use stdlib */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* HAL */
#define LV_DEF_REFR_PERIOD     16
#define LV_INDEV_DEF_READ_PERIOD 30

/* Tick — we provide tick via lv_tick_set_cb */
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)(esp_timer_get_time() / 1000ULL))

/* Performance */
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0
#define LV_USE_LOG              0

/* Fonts */
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* Core widgets */
#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_BUTTON           1
#define LV_USE_BUTTONMATRIX     1
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_IMAGE            1
#define LV_USE_LABEL            1
#define LV_USE_LINE             1
#define LV_USE_ROLLER           1
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_TABLE            1
#define LV_USE_TEXTAREA         1

/* Extra widgets */
#define LV_USE_ANIMIMAGE        0
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0
#define LV_USE_IMAGEBUTTON      0
#define LV_USE_KEYBOARD         1
#define LV_USE_LED              0
#define LV_USE_LIST             1
#define LV_USE_MENU             1
#define LV_USE_MSGBOX           1
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          1
#define LV_USE_TABVIEW          1
#define LV_USE_TILEVIEW         0
#define LV_USE_WINDOW           0

/* Themes */
#define LV_USE_THEME_DEFAULT    1

/* OS — use FreeRTOS */
#define LV_USE_OS               LV_OS_FREERTOS

#endif /* LV_CONF_H */
