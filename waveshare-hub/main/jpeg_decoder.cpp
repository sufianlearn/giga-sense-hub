/**
 * @file jpeg_decoder.cpp
 * @brief JPEG to RGB565 decoder using TJpgDec (ROM or component).
 */
#include "jpeg_decoder.h"
#include "definitions.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

/* ESP-IDF includes TJpgDec in ROM for ESP32-S3 */
#include "rom/tjpgd.h"

typedef struct {
    const uint8_t *data;
    int            len;
    int            pos;
    uint16_t      *rgb_out;
    int            out_w;
    int            out_h;
} jpeg_session_t;

static SemaphoreHandle_t s_jpeg_mutex = NULL;

static unsigned int tjpgd_input(JDEC *jd, uint8_t *buff, unsigned int nbyte)
{
    jpeg_session_t *s = (jpeg_session_t *)jd->device;
    int remaining = s->len - s->pos;
    if (remaining <= 0) return 0;
    if ((int)nbyte > remaining) nbyte = remaining;
    if (buff) {
        memcpy(buff, s->data + s->pos, nbyte);
    }
    s->pos += nbyte;
    return nbyte;
}

static unsigned int tjpgd_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_session_t *s = (jpeg_session_t *)jd->device;
    int w = rect->right - rect->left + 1;

    if (rect->left < 0 || rect->top < 0 || rect->right >= s->out_w || rect->bottom >= s->out_h) {
        ESP_LOGE(TAG, "JPEG output rect out of bounds: L%d T%d R%d B%d out=%dx%d",
                 rect->left, rect->top, rect->right, rect->bottom, s->out_w, s->out_h);
        return 0;
    }

#if JD_FORMAT == 0
    /* ROM TJpgDec outputs RGB888 (3 bytes/pixel) — convert to RGB565 */
    uint8_t *src = (uint8_t *)bitmap;
    for (int y = rect->top; y <= rect->bottom; y++) {
        int dst_offset = y * s->out_w + rect->left;
        for (int x = 0; x < w; x++) {
            uint8_t r = *src++;
            uint8_t g = *src++;
            uint8_t b = *src++;
            s->rgb_out[dst_offset + x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
#else
    /* JD_FORMAT == 1: already RGB565 */
    uint16_t *src = (uint16_t *)bitmap;
    for (int y = rect->top; y <= rect->bottom; y++) {
        int dst_offset = y * s->out_w + rect->left;
        memcpy(&s->rgb_out[dst_offset], src, w * 2);
        src += w;
    }
#endif
    return 1;
}

bool jpeg_decode_to_rgb565(const uint8_t *jpeg_data, int jpeg_len,
                            uint16_t *rgb_out, int *out_w, int *out_h)
{
    if (!s_jpeg_mutex) {
        s_jpeg_mutex = xSemaphoreCreateMutex();
        if (!s_jpeg_mutex) {
            ESP_LOGE(TAG, "JPEG mutex allocation failed");
            return false;
        }
    }
    if (xSemaphoreTake(s_jpeg_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "JPEG decoder mutex timeout");
        return false;
    }

    jpeg_session_t sess = {
        .data    = jpeg_data,
        .len     = jpeg_len,
        .pos     = 0,
        .rgb_out = rgb_out,
        .out_w   = 0,
        .out_h   = 0,
    };

    /* Work buffer — TJpgDec needs ~3KB */
    static uint8_t work[4096];
    JDEC jd;

    JRESULT rc = jd_prepare(&jd, tjpgd_input, work, sizeof(work), &sess);
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "JPEG prepare failed: %d", rc);
        xSemaphoreGive(s_jpeg_mutex);
        return false;
    }

    uint8_t scale = 0;
    while (((jd.width >> scale) > CAM_FRAME_W || (jd.height >> scale) > CAM_FRAME_H) && scale < 3) {
        scale++;
    }

    sess.out_w = jd.width >> scale;
    sess.out_h = jd.height >> scale;
    if (sess.out_w <= 0 || sess.out_h <= 0 || sess.out_w > CAM_FRAME_W || sess.out_h > CAM_FRAME_H) {
        ESP_LOGE(TAG, "JPEG dimensions unsupported: src=%ux%u scale=%u out=%dx%d max=%dx%d",
                 jd.width, jd.height, scale, sess.out_w, sess.out_h, CAM_FRAME_W, CAM_FRAME_H);
        xSemaphoreGive(s_jpeg_mutex);
        return false;
    }

    if (out_w) *out_w = sess.out_w;
    if (out_h) *out_h = sess.out_h;

    rc = jd_decomp(&jd, tjpgd_output, scale);  /* 0=1:1, 1=1/2, 2=1/4, 3=1/8 */
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "JPEG decompress failed: %d", rc);
        xSemaphoreGive(s_jpeg_mutex);
        return false;
    }

    xSemaphoreGive(s_jpeg_mutex);
    return true;
}
