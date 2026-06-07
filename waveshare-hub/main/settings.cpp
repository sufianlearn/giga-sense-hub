/**
 * @file settings.cpp
 * @brief NVS-backed persistent settings with sensible defaults.
 */
#include "settings.h"
#include "definitions.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>

#define NVS_NAMESPACE "gsh_cfg"

static nvs_handle_t s_nvs = 0;
static char s_pin_hash[65] = {0};  /* hex string of SHA-256 */

/* Default PIN hash for "1234" */
static const char *DEFAULT_PIN_HASH =
    "03ac674216f3e15c761ee1a5e255f067953623c8b388b4459e13f978d7c846f4";

static void sha256_hex(const char *input, char *out_hex)
{
    uint8_t hash[32];
    mbedtls_sha256((const uint8_t *)input, strlen(input), hash, 0);
    for (int i = 0; i < 32; i++) {
        sprintf(out_hex + i * 2, "%02x", hash[i]);
    }
    out_hex[64] = '\0';
}

void settings_init(void)
{
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    /* Load PIN hash or set default */
    size_t len = sizeof(s_pin_hash);
    err = nvs_get_str(s_nvs, "pin_hash", s_pin_hash, &len);
    if (err != ESP_OK) {
        strncpy(s_pin_hash, DEFAULT_PIN_HASH, sizeof(s_pin_hash));
        nvs_set_str(s_nvs, "pin_hash", s_pin_hash);
        nvs_commit(s_nvs);
    }

    ESP_LOGI(TAG, "Settings loaded from NVS");
}

int settings_get_brightness(void)
{
    int32_t val = 80;
    nvs_get_i32(s_nvs, "brightness", &val);
    return (int)val;
}

void settings_set_brightness(int val)
{
    if (val < 0) val = 0;
    if (val > 100) val = 100;
    nvs_set_i32(s_nvs, "brightness", val);
    nvs_commit(s_nvs);
}

int settings_get_dim_timeout(void)
{
    int32_t val = 60;
    nvs_get_i32(s_nvs, "dim_sec", &val);
    return (int)val;
}

void settings_set_dim_timeout(int sec)
{
    nvs_set_i32(s_nvs, "dim_sec", sec);
    nvs_commit(s_nvs);
}

int settings_get_off_timeout(void)
{
    int32_t val = 180;
    nvs_get_i32(s_nvs, "off_sec", &val);
    return (int)val;
}

void settings_set_off_timeout(int sec)
{
    nvs_set_i32(s_nvs, "off_sec", sec);
    nvs_commit(s_nvs);
}

int settings_get_language(void)
{
    int32_t val = 0;
    nvs_get_i32(s_nvs, "lang", &val);
    return (int)val;
}

void settings_set_language(int lang)
{
    nvs_set_i32(s_nvs, "lang", lang);
    nvs_commit(s_nvs);
}

const char *settings_get_pin_hash(void)
{
    return s_pin_hash;
}

void settings_set_pin(const char *pin)
{
    sha256_hex(pin, s_pin_hash);
    nvs_set_str(s_nvs, "pin_hash", s_pin_hash);
    nvs_commit(s_nvs);
    ESP_LOGI(TAG, "PIN updated in NVS");
}
