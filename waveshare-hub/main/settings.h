#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Persistent settings stored in NVS flash.
 *        Survives power cycles and OTA updates.
 */

void settings_init(void);

/* Brightness (0-100) */
int  settings_get_brightness(void);
void settings_set_brightness(int val);

/* Idle timeouts (seconds) */
int  settings_get_dim_timeout(void);
void settings_set_dim_timeout(int sec);
int  settings_get_off_timeout(void);
void settings_set_off_timeout(int sec);

/* Language (0=EN, 1=DE) */
int  settings_get_language(void);
void settings_set_language(int lang);

/* PIN (stored as SHA-256 hash) */
const char *settings_get_pin_hash(void);
void settings_set_pin(const char *pin);

#ifdef __cplusplus
}
#endif
