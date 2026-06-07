/**
 * @file weather_code_funcs.c
 * @brief Extracted weather code functions for host-side testing.
 *        These are exact copies from weather.cpp — keep in sync.
 */
#include <stdint.h>
#include <string.h>

/* LVGL symbol stubs — must match test_weather_codes.c */
#define LV_SYMBOL_CHARGE   "\xEF\x83\xA7"
#define LV_SYMBOL_IMAGE    "\xEF\x80\xBE"
#define LV_SYMBOL_EYE_CLOSE "\xEF\x81\xB0"
#define LV_SYMBOL_DOWN     "\xEF\x81\xB8"
#define LV_SYMBOL_MINUS    "\xEF\x81\xA8"
#define LV_SYMBOL_WARNING  "\xEF\x81\x71"
#define LV_SYMBOL_DUMMY    "\xEF\x80\x80"

const char *weather_code_to_icon(int code)
{
    if (code == 0) return LV_SYMBOL_CHARGE;
    if (code == 1) return LV_SYMBOL_CHARGE;
    if (code == 2) return LV_SYMBOL_IMAGE;
    if (code == 3) return LV_SYMBOL_IMAGE;
    if (code >= 45 && code <= 48) return LV_SYMBOL_EYE_CLOSE;
    if (code >= 51 && code <= 57) return LV_SYMBOL_DOWN;
    if (code >= 61 && code <= 67) return LV_SYMBOL_DOWN;
    if (code >= 71 && code <= 77) return LV_SYMBOL_MINUS;
    if (code >= 80 && code <= 82) return LV_SYMBOL_DOWN;
    if (code >= 85 && code <= 86) return LV_SYMBOL_MINUS;
    if (code >= 95) return LV_SYMBOL_WARNING;
    return LV_SYMBOL_DUMMY;
}

uint32_t weather_code_to_color(int code)
{
    if (code == 0 || code == 1) return 0xFFD700;
    if (code == 2)              return 0xFFAA33;
    if (code == 3)              return 0x888888;
    if (code >= 45 && code <= 48) return 0xAAAAAA;
    if (code >= 51 && code <= 57) return 0x44AAFF;
    if (code >= 61 && code <= 67) return 0x2288DD;
    if (code >= 71 && code <= 86) return 0xCCDDFF;
    if (code >= 95)              return 0xFF4444;
    return 0xFFFFFF;
}
