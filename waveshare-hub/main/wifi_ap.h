#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void wifi_ap_init(void);
bool wifi_ap_is_started(void);
bool wifi_sta_is_connected(void);
int  wifi_ap_get_sta_count(void);

#ifdef __cplusplus
}
#endif
