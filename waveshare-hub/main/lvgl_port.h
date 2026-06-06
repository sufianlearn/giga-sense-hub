#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t lvgl_mux;

void lvgl_port_init(void);
void lvgl_port_start_task(void);
bool lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
