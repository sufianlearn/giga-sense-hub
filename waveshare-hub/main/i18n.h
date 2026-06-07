#pragma once

/**
 * @file i18n.h
 * @brief Localization — English and German string tables.
 *        Compile-time string IDs, runtime language switch.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LANG_EN = 0, LANG_DE = 1 } lang_t;

void i18n_set_language(lang_t lang);
lang_t i18n_get_language(void);

/* String IDs */
enum {
    S_SETTINGS = 0,
    S_NETWORK,
    S_CAMERAS,
    S_DISPLAY,
    S_SECURITY,
    S_SYSTEM_INFO,
    S_ABOUT,
    S_BACK,
    S_BRIGHTNESS,
    S_DIM_TIMEOUT,
    S_OFF_TIMEOUT,
    S_LANGUAGE,
    S_REBOOT,
    S_OTA_UPDATE,
    S_FREE_HEAP,
    S_UPTIME,
    S_WIFI_RSSI,
    S_FPS,
    S_NODES_CONNECTED,
    S_AUTHENTICATION,
    S_PIN_LOCK,
    S_WEATHER,
    S_WIND,
    S_LOADING,
    S_ONLINE,
    S_AP_ONLY,
    S_ACTIVE,
    S_OFFLINE,
    S_CONNECTED,
    S_DISCONNECTED,
    S_NODE,
    S_MODE_AP,
    S_CHANNEL,
    S_FIRMWARE,
    S_BUILT_WITH,
    S_SELECT_NODE,
    S_UPDATE_FW,
    S_SECONDS,
    S__COUNT
};

const char *i18n(int str_id);

#ifdef __cplusplus
}
#endif
