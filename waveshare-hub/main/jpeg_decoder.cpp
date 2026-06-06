/**
 * @file jpeg_decoder.cpp
 * @brief JPEG to RGB565 decoder using TJpgDec (ROM or component).
 */
#include "jpeg_decoder.h"
#include "definitions.h"
#include "esp_log.h"
#include <string.h>

/* ESP-IDF includes TJpgDec in ROM for ESP32-S3 */
#include "rom/tjpgd.h"

typedef struct {
    const uint8_t *data;
    int            len;
    int            pos;
    uint16_t      *rgb_out;
    int            out_w;
} jpeg_session_t;

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
    uint16_t *src = (uint16_t *)bitmap;
    int w = rect->right - rect->left + 1;

    for (int y = rect->top; y <= rect->bottom; y++) {
        int dst_offset = y * s->out_w + rect->left;
        memcpy(&s->rgb_out[dst_offset], src, w * 2);
        src += w;
    }
    return 1;
}

bool jpeg_decode_to_rgb565(const uint8_t *jpeg_data, int jpeg_len,
                            uint16_t *rgb_out, int *out_w, int *out_h)
{
    jpeg_session_t sess = {
        .data    = jpeg_data,
        .len     = jpeg_len,
        .pos     = 0,
        .rgb_out = rgb_out,
        .out_w   = 0,
    };

    /* Work buffer — TJpgDec needs ~3KB */
    static uint8_t work[4096];
    JDEC jd;

    JRESULT rc = jd_prepare(&jd, tjpgd_input, work, sizeof(work), &sess);
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "JPEG prepare failed: %d", rc);
        return false;
    }

    sess.out_w = jd.width;
    if (out_w) *out_w = jd.width;
    if (out_h) *out_h = jd.height;

    rc = jd_decomp(&jd, tjpgd_output, 0);  /* scale 0 = 1:1 */
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "JPEG decompress failed: %d", rc);
        return false;
    }

    return true;
}
