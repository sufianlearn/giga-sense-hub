#pragma once

/* ── Display geometry ──────────────────────────────────────────────── */
#define LCD_H_RES               800
#define LCD_V_RES               480
#define LCD_PIXEL_CLOCK_HZ      (18 * 1000 * 1000)
#define LCD_NUM_FB              2

/* ── Backlight ─────────────────────────────────────────────────────── */
#define PIN_NUM_BK_LIGHT        (-1)
#define LCD_BK_LIGHT_ON_LEVEL   (1)

/* ── RGB panel GPIOs ───────────────────────────────────────────────── */
#define PIN_NUM_HSYNC           46
#define PIN_NUM_VSYNC           3
#define PIN_NUM_DE              5
#define PIN_NUM_PCLK            7
#define PIN_NUM_DISP_EN         (-1)

#define PIN_NUM_DATA0           14   /* B3 */
#define PIN_NUM_DATA1           38   /* B4 */
#define PIN_NUM_DATA2           18   /* B5 */
#define PIN_NUM_DATA3           17   /* B6 */
#define PIN_NUM_DATA4           10   /* B7 */
#define PIN_NUM_DATA5           39   /* G2 */
#define PIN_NUM_DATA6           0    /* G3 */
#define PIN_NUM_DATA7           45   /* G4 */
#define PIN_NUM_DATA8           48   /* G5 */
#define PIN_NUM_DATA9           47   /* G6 */
#define PIN_NUM_DATA10          21   /* G7 */
#define PIN_NUM_DATA11          1    /* R3 */
#define PIN_NUM_DATA12          2    /* R4 */
#define PIN_NUM_DATA13          42   /* R5 */
#define PIN_NUM_DATA14          41   /* R6 */
#define PIN_NUM_DATA15          40   /* R7 */

/* ── I2C (touch) ───────────────────────────────────────────────────── */
#define I2C_MASTER_SCL_IO       9
#define I2C_MASTER_SDA_IO       8
#define I2C_MASTER_NUM          0
#define I2C_MASTER_FREQ_HZ     400000
#define I2C_MASTER_TIMEOUT_MS  1000

/* ── LVGL task / tick ──────────────────────────────────────────────── */
#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  1
#define LVGL_TASK_STACK_SIZE    (8 * 1024)
#define LVGL_TASK_PRIORITY      2

/* ── Camera stream ─────────────────────────────────────────────────── */
#define MAX_NODES               2
#define JPEG_BUF_SIZE           (80 * 1024)
#define CAM_FRAME_W             320
#define CAM_FRAME_H             240

/* ── Network ───────────────────────────────────────────────────────── */
#define AP_SSID_WS              "GigaSenseHub"
#define AP_PASS_WS              "sensehub32"
#define AP_CHANNEL_WS           1
#define AP_MAX_CONN             4
#define AP_IP                   "192.168.4.1"
#define NODE_BASE_IP            "192.168.4.2"

/* ── Weather ───────────────────────────────────────────────────────── */
#define WEATHER_CITY            "Moosburg an der Isar"
#define WEATHER_LAT             "48.4681"
#define WEATHER_LON             "11.9381"

/* ── Logging tag ───────────────────────────────────────────────────── */
#define TAG                     "gsh_ws"
