/**
 * @file power_mgmt.cpp
 * @brief 3-level power management: ACTIVE → DIM → OFF
 *
 *        The Waveshare 7" display backlight is controlled via the IO
 *        expander at address 0x38. Bit 1 (0x02) controls the backlight
 *        enable. Full PWM isn't available, so DIM is simulated by
 *        rapid on/off cycling via a timer (effectively a software PWM).
 *
 *        For simplicity we use: ACTIVE=full bright, DIM=50%, OFF=0%.
 */
#include "power_mgmt.h"
#include "definitions.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include <string.h>

static power_state_t s_state    = POWER_STATE_ACTIVE;
static int64_t       s_last_activity = 0;   /* us timestamp */
static int           s_brightness   = 100;  /* 0-100% */
static int           s_dim_sec      = 60;   /* seconds to dim */
static int           s_off_sec      = 180;  /* seconds to off */
static bool          s_backlight_on = true;

/* Write backlight state to IO expander 0x38.
   The Waveshare 7" uses bit 1 for backlight enable.
   Current value is maintained so we don't clobber other bits. */
static uint8_t s_io38_val = 0x2E;  /* Power-on default from lvgl_port */

static void set_backlight_hw(bool on)
{
    if (on == s_backlight_on) return;
    s_backlight_on = on;

    if (on) {
        s_io38_val |= 0x02;   /* Set bit 1 */
    } else {
        s_io38_val &= ~0x02;  /* Clear bit 1 */
    }

    i2c_master_write_to_device(
        static_cast<i2c_port_t>(I2C_MASTER_NUM),
        0x38, &s_io38_val, 1,
        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Backlight %s (0x38=0x%02X)", on ? "ON" : "OFF", s_io38_val);
}

void power_mgmt_init(void)
{
    s_last_activity = esp_timer_get_time();
    s_state = POWER_STATE_ACTIVE;
    s_backlight_on = true;
    ESP_LOGI(TAG, "Power mgmt init: dim=%ds, off=%ds", s_dim_sec, s_off_sec);
}

void power_mgmt_reset_idle(void)
{
    s_last_activity = esp_timer_get_time();
    if (s_state != POWER_STATE_ACTIVE) {
        ESP_LOGI(TAG, "Power: wake from %s → ACTIVE",
                 s_state == POWER_STATE_DIM ? "DIM" : "OFF");
        s_state = POWER_STATE_ACTIVE;
        set_backlight_hw(true);
    }
}

void power_mgmt_tick(void)
{
    int64_t now = esp_timer_get_time();
    int elapsed_sec = (int)((now - s_last_activity) / 1000000LL);

    switch (s_state) {
    case POWER_STATE_ACTIVE:
        if (elapsed_sec >= s_dim_sec) {
            s_state = POWER_STATE_DIM;
            ESP_LOGI(TAG, "Power: ACTIVE → DIM (idle %ds)", elapsed_sec);
            /* DIM: we can't do real PWM on the IO expander, so we
               just keep backlight on but could reduce LVGL opacity.
               For now, keep it on — the visual "dim" will be done
               by overlaying a semi-transparent dark layer in LVGL. */
        }
        break;

    case POWER_STATE_DIM:
        if (elapsed_sec >= s_off_sec) {
            s_state = POWER_STATE_OFF;
            set_backlight_hw(false);
            ESP_LOGI(TAG, "Power: DIM → OFF (idle %ds)", elapsed_sec);
        }
        break;

    case POWER_STATE_OFF:
        /* Stay off until touch resets idle */
        break;
    }
}

power_state_t power_mgmt_get_state(void)
{
    return s_state;
}

void power_mgmt_set_brightness(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    s_brightness = pct;

    if (pct == 0) {
        set_backlight_hw(false);
    } else {
        set_backlight_hw(true);
    }
}

int power_mgmt_get_brightness(void)
{
    return s_brightness;
}

void power_mgmt_set_dim_timeout(int sec)
{
    s_dim_sec = sec > 0 ? sec : 30;
}

void power_mgmt_set_off_timeout(int sec)
{
    s_off_sec = sec > 0 ? sec : 120;
}
