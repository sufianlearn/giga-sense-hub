/**
 * @file test_i18n.c
 * @brief Unit tests for i18n string table — runs on host (no ESP-IDF).
 *        Compile: gcc -I../main -o test_i18n test_i18n.c ../main/i18n.cpp
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "i18n.h"

static int passed = 0, failed = 0;

#define TEST(name) printf("  %-40s ", name);
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); failed++; } while(0)
#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { FAIL("expected '" b "'"); return; } \
} while(0)
#define ASSERT_NOT_NULL(a) do { \
    if ((a) == NULL) { FAIL("got NULL"); return; } \
} while(0)

static void test_default_language(void)
{
    TEST("default language is English");
    assert(i18n_get_language() == LANG_EN);
    PASS();
}

static void test_english_strings(void)
{
    TEST("English string lookup");
    i18n_set_language(LANG_EN);
    ASSERT_STR_EQ(i18n(S_SETTINGS), "Settings");
    ASSERT_STR_EQ(i18n(S_NETWORK), "Network");
    ASSERT_STR_EQ(i18n(S_CAMERAS), "Cameras");
    ASSERT_STR_EQ(i18n(S_REBOOT), "Reboot");
    ASSERT_STR_EQ(i18n(S_WEATHER), "Weather");
    PASS();
}

static void test_german_strings(void)
{
    TEST("German string lookup");
    i18n_set_language(LANG_DE);
    ASSERT_STR_EQ(i18n(S_SETTINGS), "Einstellungen");
    ASSERT_STR_EQ(i18n(S_NETWORK), "Netzwerk");
    ASSERT_STR_EQ(i18n(S_CAMERAS), "Kameras");
    ASSERT_STR_EQ(i18n(S_REBOOT), "Neustart");
    ASSERT_STR_EQ(i18n(S_WEATHER), "Wetter");
    PASS();
}

static void test_language_toggle(void)
{
    TEST("language toggle round-trip");
    i18n_set_language(LANG_EN);
    assert(i18n_get_language() == LANG_EN);
    ASSERT_STR_EQ(i18n(S_BACK), "Back");

    i18n_set_language(LANG_DE);
    assert(i18n_get_language() == LANG_DE);
    /* Zurück — UTF-8 encoded */
    ASSERT_NOT_NULL(i18n(S_BACK));

    i18n_set_language(LANG_EN);
    ASSERT_STR_EQ(i18n(S_BACK), "Back");
    PASS();
}

static void test_invalid_id(void)
{
    TEST("invalid string ID returns ???");
    i18n_set_language(LANG_EN);
    ASSERT_STR_EQ(i18n(-1), "???");
    ASSERT_STR_EQ(i18n(9999), "???");
    PASS();
}

static void test_all_ids_non_null(void)
{
    TEST("all EN string IDs are non-NULL");
    i18n_set_language(LANG_EN);
    for (int i = 0; i < S__COUNT; i++) {
        if (i18n(i) == NULL) { FAIL("NULL string"); return; }
    }
    PASS();

    TEST("all DE string IDs are non-NULL");
    i18n_set_language(LANG_DE);
    for (int i = 0; i < S__COUNT; i++) {
        if (i18n(i) == NULL) { FAIL("NULL string"); return; }
    }
    PASS();
}

int main(void)
{
    printf("=== i18n Tests ===\n");
    test_default_language();
    test_english_strings();
    test_german_strings();
    test_language_toggle();
    test_invalid_id();
    test_all_ids_non_null();
    printf("\nResults: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
