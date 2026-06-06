#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
lv_obj_t *ui_dashboard_create(void);
void ui_dashboard_update_cam(int node_idx, uint16_t *rgb_data, int w, int h);
void ui_dashboard_update_weather(void);
void ui_dashboard_update_status(void);
#ifdef __cplusplus
}
#endif
