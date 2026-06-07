#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power management — 3-level idle state machine:
 *        ACTIVE → DIM (after idle_dim_sec) → OFF (after idle_off_sec)
 *
 *        Touch activity resets the idle timer.
 *        Backlight controlled via Waveshare IO expander (0x38).
 */

void power_mgmt_init(void);

/* Call from touch handler or any user interaction */
void power_mgmt_reset_idle(void);

/* Call periodically from LVGL task */
void power_mgmt_tick(void);

/* Current state query */
typedef enum {
    POWER_STATE_ACTIVE = 0,
    POWER_STATE_DIM,
    POWER_STATE_OFF,
} power_state_t;

power_state_t power_mgmt_get_state(void);

/* Brightness control (0-100%) */
void power_mgmt_set_brightness(int pct);
int  power_mgmt_get_brightness(void);

/* Idle timeout config (seconds) */
void power_mgmt_set_dim_timeout(int sec);
void power_mgmt_set_off_timeout(int sec);

#ifdef __cplusplus
}
#endif
