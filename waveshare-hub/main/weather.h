#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature;
    float humidity;
    float wind_speed;
    int   weather_code;
    char  description[64];
    char  city[32];
    bool  valid;
} weather_data_t;

extern weather_data_t g_weather;

/**
 * Fetch weather from Open-Meteo (no API key needed).
 * Runs in its own task, updates g_weather periodically.
 */
void weather_task_start(void);

const char *weather_code_to_icon(int code);
const char *weather_code_to_desc(int code);

#ifdef __cplusplus
}
#endif
