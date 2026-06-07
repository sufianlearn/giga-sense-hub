#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature;      /* °C */
    float humidity;         /* % */
    float wind_speed;       /* km/h */
    int   weather_code;     /* WMO code */
    bool  valid;
    char  description[32];
} weather_data_t;

void           weather_init(void);
weather_data_t weather_get(void);
bool           weather_fetch(void);  /* blocking HTTP fetch */
const char    *weather_code_to_icon(int code);
uint32_t       weather_code_to_color(int code);

#ifdef __cplusplus
}
#endif
