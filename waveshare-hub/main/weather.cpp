/**
 * @file weather.cpp
 * @brief Fetch weather from Open-Meteo API (free, no key needed).
 *        Uses plain HTTP (port 80) — no TLS overhead on ESP32.
 *
 *        API: http://api.open-meteo.com/v1/forecast
 *             ?latitude=48.1351&longitude=11.5820
 *             &current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code
 */
#include "weather.h"
#include "definitions.h"
#include "wifi_ap.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static weather_data_t s_weather = {};
static char s_resp_buf[1024];
static int  s_resp_len = 0;

/* WMO weather code to short description */
static const char *wmo_description(int code)
{
    switch (code) {
    case 0:  return "Clear sky";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: case 63: case 65: return "Rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: return "Snow";
    case 77: return "Snow grains";
    case 80: case 81: case 82: return "Rain showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm+hail";
    default: return "Unknown";
    }
}

/* WMO weather code to LVGL symbol */
const char *weather_code_to_icon(int code)
{
    if (code == 0 || code == 1) return LV_SYMBOL_CHARGE;  /* sun-like */
    if (code == 2 || code == 3) return LV_SYMBOL_IMAGE;   /* cloud-like */
    if (code >= 45 && code <= 48) return LV_SYMBOL_EYE_CLOSE; /* fog */
    if (code >= 51 && code <= 67) return LV_SYMBOL_DOWN;   /* rain */
    if (code >= 71 && code <= 86) return LV_SYMBOL_DOWN;   /* snow */
    if (code >= 95) return LV_SYMBOL_WARNING;              /* thunder */
    return LV_SYMBOL_DUMMY;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (s_resp_len + evt->data_len < (int)sizeof(s_resp_buf) - 1) {
            memcpy(s_resp_buf + s_resp_len, evt->data, evt->data_len);
            s_resp_len += evt->data_len;
            s_resp_buf[s_resp_len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* Extract a float value from JSON like "key":12.3 */
static bool json_get_float(const char *json, const char *key, float *out)
{
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ') p++;
    *out = strtof(p, NULL);
    return true;
}

static bool json_get_int(const char *json, const char *key, int *out)
{
    float f;
    if (!json_get_float(json, key, &f)) return false;
    *out = (int)f;
    return true;
}

void weather_init(void)
{
    memset(&s_weather, 0, sizeof(s_weather));
    ESP_LOGI(TAG, "Weather module init — %s (%.4s, %.5s)",
             WEATHER_CITY, WEATHER_LAT, WEATHER_LON);
}

weather_data_t weather_get(void)
{
    return s_weather;
}

bool weather_fetch(void)
{
    if (!wifi_sta_is_connected()) {
        ESP_LOGW(TAG, "Weather: no internet connection (STA not connected)");
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%s&longitude=%s"
        "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code",
        WEATHER_LAT, WEATHER_LON);

    s_resp_len = 0;
    memset(s_resp_buf, 0, sizeof(s_resp_buf));

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Weather: HTTP client init failed");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Weather: HTTP error=%s status=%d", esp_err_to_name(err), status);
        return false;
    }

    ESP_LOGI(TAG, "Weather: got %d bytes, status=%d", s_resp_len, status);

    /* Parse the "current" block */
    const char *current = strstr(s_resp_buf, "\"current\"");
    if (!current) {
        ESP_LOGE(TAG, "Weather: no 'current' in response");
        return false;
    }

    weather_data_t w = {};
    json_get_float(current, "temperature_2m", &w.temperature);
    json_get_float(current, "relative_humidity_2m", &w.humidity);
    json_get_float(current, "wind_speed_10m", &w.wind_speed);
    json_get_int(current, "weather_code", &w.weather_code);

    strncpy(w.description, wmo_description(w.weather_code), sizeof(w.description) - 1);
    w.valid = true;

    s_weather = w;
    ESP_LOGI(TAG, "Weather: %.1f°C, %s, humidity=%.0f%%, wind=%.1fkm/h",
             w.temperature, w.description, w.humidity, w.wind_speed);
    return true;
}
