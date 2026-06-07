/**
 * @file lvgl_port.cpp
 * @brief LVGL 9 hardware-port: RGB LCD, GT911 touch, tick, task, mutex.
 *        Ported from LVGL 8.3 baseline — Waveshare 7" ESP32-S3.
 */
#include "lvgl_port.h"
#include "definitions.h"
#include "power_mgmt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"

#include "lvgl.h"
#include <assert.h>

SemaphoreHandle_t lvgl_mux = NULL;

static lv_display_t       *s_disp = NULL;
static SemaphoreHandle_t   s_flush_sem = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;

/* VSYNC callback — signals that a full DMA frame transfer has
   completed and LVGL can safely write to the PSRAM framebuffer. */
static bool IRAM_ATTR bounce_frame_done_cb(esp_lcd_panel_handle_t,
                                            const esp_lcd_rgb_panel_event_data_t *,
                                            void *)
{
    BaseType_t woken = pdFALSE;
    if (s_flush_sem) xSemaphoreGiveFromISR(s_flush_sem, &woken);
    return woken == pdTRUE;
}

/* LVGL 9 tick callback — returns elapsed ms since boot */
static uint32_t lvgl_tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* LVGL 9 flush callback — signature uses uint8_t* instead of lv_color_t* */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *px_map)
{
    /* Wait up to 50ms for DMA frame boundary to prevent tearing */
    if (s_flush_sem) {
        xSemaphoreTake(s_flush_sem, pdMS_TO_TICKS(50));
    }

    esp_lcd_panel_draw_bitmap(s_panel,
                               area->x1, area->y1,
                               area->x2 + 1, area->y2 + 1,
                               px_map);
    lv_display_flush_ready(disp);
}

/* LVGL 9 touch read callback */
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp =
        static_cast<esp_lcd_touch_handle_t>(lv_indev_get_user_data(indev));

    uint16_t x[1] = {0}, y[1] = {0};
    uint8_t  cnt  = 0;

    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, x, y, NULL, &cnt, 1);

    if (pressed && cnt > 0) {
        data->point.x = x[0];
        data->point.y = y[0];
        data->state   = LV_INDEV_STATE_PRESSED;
        power_mgmt_reset_idle();  /* Wake from dim/off on touch */
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA_IO,
        .scl_io_num       = I2C_MASTER_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master           = { .clk_speed = I2C_MASTER_FREQ_HZ },
        .clk_flags        = 0,
    };
    i2c_param_config(static_cast<i2c_port_t>(I2C_MASTER_NUM), &conf);
    ESP_ERROR_CHECK(
        i2c_driver_install(static_cast<i2c_port_t>(I2C_MASTER_NUM),
                           conf.mode, 0, 0, 0));
    ESP_LOGI(TAG, "I2C initialized");
}

static void lvgl_port_task(void *)
{
    ESP_LOGI(TAG, "LVGL port task started");
    uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;

    while (true) {
        if (lvgl_port_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        delay_ms = (delay_ms > LVGL_TASK_MAX_DELAY_MS)
                       ? LVGL_TASK_MAX_DELAY_MS
                   : (delay_ms < LVGL_TASK_MIN_DELAY_MS)
                       ? LVGL_TASK_MIN_DELAY_MS
                       : delay_ms;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void lvgl_port_init(void)
{
    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* RGB LCD panel — exact logic_suite timing and framebuffer mode */
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src  = LCD_CLK_SRC_DEFAULT,
        .timings  = {
            .pclk_hz          = LCD_PIXEL_CLOCK_HZ,
            .h_res            = LCD_H_RES,
            .v_res            = LCD_V_RES,
            .hsync_pulse_width = 5,
            .hsync_back_porch  = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch  = 16,
            .vsync_front_porch = 16,
            .flags = {
                .hsync_idle_low  = true,
                .vsync_idle_low  = true,
                .de_idle_high    = true,
                .pclk_active_neg = true,
            },
        },
        .data_width       = 16,
        .bits_per_pixel   = 0,
        .num_fbs          = LCD_NUM_FB,
        .bounce_buffer_size_px = LCD_H_RES * 10,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num   = PIN_NUM_HSYNC,
        .vsync_gpio_num   = PIN_NUM_VSYNC,
        .de_gpio_num      = PIN_NUM_DE,
        .pclk_gpio_num    = PIN_NUM_PCLK,
        .disp_gpio_num    = PIN_NUM_DISP_EN,
        .data_gpio_nums   = {
            PIN_NUM_DATA0,  PIN_NUM_DATA1,
            PIN_NUM_DATA2,  PIN_NUM_DATA3,
            PIN_NUM_DATA4,  PIN_NUM_DATA5,
            PIN_NUM_DATA6,  PIN_NUM_DATA7,
            PIN_NUM_DATA8,  PIN_NUM_DATA9,
            PIN_NUM_DATA10, PIN_NUM_DATA11,
            PIN_NUM_DATA12, PIN_NUM_DATA13,
            PIN_NUM_DATA14, PIN_NUM_DATA15,
        },
        .flags = {
            .disp_active_low   = false,
            .refresh_on_demand = false,
            .fb_in_psram       = true,
            .double_fb         = (LCD_NUM_FB == 2),
            .no_fb             = false,
            .bb_invalidate_cache = true,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &s_panel));

    /* Register VSYNC callback for flush pacing */
    s_flush_sem = xSemaphoreCreateBinary();
    const esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = bounce_frame_done_cb,
        .on_bounce_empty = NULL,
        .on_bounce_frame_finish = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(
        s_panel, &cbs, NULL));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    /* I2C + touch */
    i2c_master_init();

    /* Waveshare 7" touch power-on sequence via IO expanders */
    uint8_t buf = 0x01;
    i2c_master_write_to_device(static_cast<i2c_port_t>(I2C_MASTER_NUM),
                               0x24, &buf, 1,
                               I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    buf = 0x2C;
    i2c_master_write_to_device(static_cast<i2c_port_t>(I2C_MASTER_NUM),
                               0x38, &buf, 1,
                               I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    esp_rom_delay_us(400 * 1000);
    buf = 0x2E;
    i2c_master_write_to_device(static_cast<i2c_port_t>(I2C_MASTER_NUM),
                               0x38, &buf, 1,
                               I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    /* GT911 touch init */
    esp_lcd_touch_handle_t tp = NULL;
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;

    {
        uint8_t dummy = 0;
        esp_err_t r = i2c_master_write_to_device(
            static_cast<i2c_port_t>(I2C_MASTER_NUM), 0x5D,
            &dummy, 0, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "GT911 probe 0x5D: %s", r == ESP_OK ? "ACK" : esp_err_to_name(r));
    }

    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
    tp_io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    tp_io_cfg.on_color_trans_done = NULL;
    tp_io_cfg.user_ctx = NULL;
    tp_io_cfg.control_phase_bytes = 1;
    tp_io_cfg.dc_bit_offset = 0;
    tp_io_cfg.lcd_cmd_bits = 16;
    tp_io_cfg.lcd_param_bits = 0;
    tp_io_cfg.flags.disable_control_phase = 1;
    esp_lcd_new_panel_io_i2c(
        static_cast<esp_lcd_i2c_bus_handle_t>(I2C_MASTER_NUM),
        &tp_io_cfg, &tp_io_handle);

    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "tp_io_handle: %p", tp_io_handle);
    if (tp_io_handle == NULL) {
        ESP_LOGE(TAG, "panel IO NULL — aborting");
        abort();
    }

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max        = LCD_V_RES;
    tp_cfg.y_max        = LCD_H_RES;
    tp_cfg.rst_gpio_num = static_cast<gpio_num_t>(-1);
    tp_cfg.int_gpio_num = static_cast<gpio_num_t>(-1);

    ESP_LOGI(TAG, "Initialize touch controller GT911");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp));

    /* ── LVGL 9 init ────────────────────────────────────── */
    lv_init();

    /* Tick — use esp_timer_get_time callback */
    lv_tick_set_cb(lvgl_tick_get_cb);

    /* Display — LVGL 9 API: lv_display_create + setters */
    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);

    /* Draw buffer in PSRAM — 100 lines.
       The VSYNC semaphore gates each flush to a frame boundary,
       preventing tearing. Larger buffer = fewer flush calls = faster. */
    static uint8_t *draw_buf = NULL;
    size_t buf_size = LCD_H_RES * 100 * sizeof(lv_color16_t);
    draw_buf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    assert(draw_buf && "LVGL draw buffer allocation failed");
    lv_display_set_buffers(s_disp, draw_buf, NULL, buf_size,
                            LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    /* Input device — LVGL 9 API */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
    lv_indev_set_user_data(indev, tp);
    lv_indev_set_display(indev, s_disp);

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux && "LVGL mutex creation failed");

    ESP_LOGI(TAG, "LVGL 9 port initialised");
}

void lvgl_port_start_task(void)
{
    xTaskCreate(lvgl_port_task, "lvgl",
                LVGL_TASK_STACK_SIZE * 2,
                NULL, LVGL_TASK_PRIORITY, NULL);
    ESP_LOGI(TAG, "LVGL task started");
}

bool lvgl_port_lock(int timeout_ms)
{
    const TickType_t ticks = (timeout_ms < 0)
                                 ? portMAX_DELAY
                                 : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    xSemaphoreGiveRecursive(lvgl_mux);
}
