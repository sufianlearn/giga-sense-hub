/**
 * @file weather.cpp
 * @brief Weather data from Open-Meteo for Moosburg an der Isar.
 *        No API key required. Updates every 10 minutes.
 *        NOTE: Requires internet — if running as AP only (no STA uplink),
 *        weather data stays invalid and shows "No data".
 */
#include "weather.h"
#include "lvgl.h"
#include "definitions.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

weather_data_t g_weather = {};

/* WMO weather codes to descriptions */
const char *weather_code_to_desc(int code)
{
    switch (code) {
        case 0:  return "Clear sky";
        case 1:  return "Mainly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45: case 48: return "Foggy";
        case 51: case 53: case 55: return "Drizzle";
        case 61: case 63: case 65: return "Rain";
        case 66: case 67: return "Freezing rain";
        case 71: case 73: case 75: return "Snow";
        case 77: return "Snow grains";
        case 80: case 81: case 82: return "Rain showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunderstorm + hail";
        default: return "Unknown";
    }
}

const char *weather_code_to_icon(int code)
{
    if (code == 0)                    return LV_SYMBOL_CHARGE;  /* sun-like */
    if (code <= 3)                    return LV_SYMBOL_EYE_OPEN;
    if (code == 45 || code == 48)     return LV_SYMBOL_EYE_CLOSE;
    if (code >= 51 && code <= 67)     return LV_SYMBOL_DOWNLOAD;  /* rain-like */
    if (code >= 71 && code <= 77)     return LV_SYMBOL_DOWNLOAD;
    if (code >= 80 && code <= 86)     return LV_SYMBOL_DOWNLOAD;
    if (code >= 95)                   return LV_SYMBOL_WARNING;
    return LV_SYMBOL_DUMMY;
}

static char s_resp_buf[2048];
static int  s_resp_len = 0;

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

static float parse_json_float(const char *json, const char *key)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return 0.0f;
    p += strlen(search);
    while (*p == ' ' || *p == '\t') p++;
    return strtof(p, NULL);
}

static int parse_json_int(const char *json, const char *key)
{
    return (int)parse_json_float(json, key);
}

static void fetch_weather(void)
{
    s_resp_len = 0;
    memset(s_resp_buf, 0, sizeof(s_resp_buf));

    esp_http_client_config_t config = {};
    /* Open-Meteo free API — no key needed */
    static const char url[] =
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=" WEATHER_LAT
        "&longitude=" WEATHER_LON
        "&current_weather=true"
        "&timezone=Europe%2FBerlin";

    config.url = url;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK && s_resp_len > 0) {
        /* Parse current_weather block */
        const char *cw = strstr(s_resp_buf, "\"current_weather\"");
        if (cw) {
            g_weather.temperature  = parse_json_float(cw, "temperature");
            g_weather.wind_speed   = parse_json_float(cw, "windspeed");
            g_weather.weather_code = parse_json_int(cw, "weathercode");
            strncpy(g_weather.description,
                    weather_code_to_desc(g_weather.weather_code),
                    sizeof(g_weather.description) - 1);
            strncpy(g_weather.city, WEATHER_CITY, sizeof(g_weather.city) - 1);
            g_weather.valid = true;
            ESP_LOGI(TAG, "Weather: %.1f°C, %s, wind %.1f km/h",
                     g_weather.temperature, g_weather.description,
                     g_weather.wind_speed);
        }
    } else {
        ESP_LOGW(TAG, "Weather fetch failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void weather_task(void *arg)
{
    /* Wait for WiFi to be up + some settle time */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (true) {
        fetch_weather();
        vTaskDelay(pdMS_TO_TICKS(600000));  /* 10 minutes */
    }
}

void weather_task_start(void)
{
    xTaskCreate(weather_task, "weather", 8192, NULL, 3, NULL);
    ESP_LOGI(TAG, "Weather task started for %s", WEATHER_CITY);
}
