/**
 * @file test_weather_codes.c
 * @brief Unit tests for WMO weather code mapping — runs on host.
 *
 *        Stubs out LVGL symbols and ESP-IDF headers so we can compile
 *        just the weather_code_to_icon/color functions on the host.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Stub LVGL symbols — just need them to be distinct non-NULL strings */
#define LV_SYMBOL_CHARGE   "\xEF\x83\xA7"
#define LV_SYMBOL_IMAGE    "\xEF\x80\xBE"
#define LV_SYMBOL_EYE_CLOSE "\xEF\x81\xB0"
#define LV_SYMBOL_DOWN     "\xEF\x81\xB8"
#define LV_SYMBOL_MINUS    "\xEF\x81\xA8"
#define LV_SYMBOL_WARNING  "\xEF\x81\x71"
#define LV_SYMBOL_DUMMY    "\xEF\x80\x80"

/* Forward declarations matching weather.cpp */
const char *weather_code_to_icon(int code);
uint32_t    weather_code_to_color(int code);

/* Pull in the functions — we'll compile weather_code_funcs.c separately */

static int passed = 0, failed = 0;

#define TEST(name) printf("  %-44s ", name);
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); failed++; } while(0)

static void test_clear_sky(void)
{
    TEST("code 0 (clear sky) -> sun icon");
    if (strcmp(weather_code_to_icon(0), LV_SYMBOL_CHARGE) != 0) { FAIL("wrong icon"); return; }
    if (weather_code_to_color(0) != 0xFFD700) { FAIL("wrong color"); return; }
    PASS();
}

static void test_partly_cloudy(void)
{
    TEST("code 2 (partly cloudy) -> cloud icon");
    if (strcmp(weather_code_to_icon(2), LV_SYMBOL_IMAGE) != 0) { FAIL("wrong icon"); return; }
    if (weather_code_to_color(2) != 0xFFAA33) { FAIL("wrong color"); return; }
    PASS();
}

static void test_overcast(void)
{
    TEST("code 3 (overcast) -> cloud icon, grey");
    if (strcmp(weather_code_to_icon(3), LV_SYMBOL_IMAGE) != 0) { FAIL("wrong icon"); return; }
    if (weather_code_to_color(3) != 0x888888) { FAIL("wrong color"); return; }
    PASS();
}

static void test_fog(void)
{
    TEST("code 45-48 (fog) -> eye-close icon");
    for (int c = 45; c <= 48; c++) {
        if (strcmp(weather_code_to_icon(c), LV_SYMBOL_EYE_CLOSE) != 0) { FAIL("wrong icon"); return; }
        if (weather_code_to_color(c) != 0xAAAAAA) { FAIL("wrong color"); return; }
    }
    PASS();
}

static void test_rain(void)
{
    TEST("code 61-67 (rain) -> down icon, blue");
    for (int c = 61; c <= 67; c++) {
        if (strcmp(weather_code_to_icon(c), LV_SYMBOL_DOWN) != 0) { FAIL("wrong icon"); return; }
        if (weather_code_to_color(c) != 0x2288DD) { FAIL("wrong color"); return; }
    }
    PASS();
}

static void test_snow(void)
{
    TEST("code 71-77 (snow) -> minus icon, ice blue");
    for (int c = 71; c <= 77; c++) {
        if (strcmp(weather_code_to_icon(c), LV_SYMBOL_MINUS) != 0) { FAIL("wrong icon"); return; }
        if (weather_code_to_color(c) != 0xCCDDFF) { FAIL("wrong color"); return; }
    }
    PASS();
}

static void test_thunderstorm(void)
{
    TEST("code 95+ (thunderstorm) -> warning icon, red");
    for (int c = 95; c <= 99; c++) {
        if (strcmp(weather_code_to_icon(c), LV_SYMBOL_WARNING) != 0) { FAIL("wrong icon"); return; }
        if (weather_code_to_color(c) != 0xFF4444) { FAIL("wrong color"); return; }
    }
    PASS();
}

static void test_unknown_code(void)
{
    TEST("unmapped code (e.g. 30) -> dummy icon, white");
    if (strcmp(weather_code_to_icon(30), LV_SYMBOL_DUMMY) != 0) { FAIL("wrong icon"); return; }
    if (weather_code_to_color(30) != 0xFFFFFF) { FAIL("wrong color"); return; }
    PASS();
}

static void test_all_wmo_codes_non_null(void)
{
    TEST("all WMO codes 0-99 return non-NULL icon");
    for (int c = 0; c < 100; c++) {
        if (weather_code_to_icon(c) == NULL) { FAIL("NULL"); return; }
    }
    PASS();
}

int main(void)
{
    printf("=== Weather Code Tests ===\n");
    test_clear_sky();
    test_partly_cloudy();
    test_overcast();
    test_fog();
    test_rain();
    test_snow();
    test_thunderstorm();
    test_unknown_code();
    test_all_wmo_codes_non_null();
    printf("\nResults: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
