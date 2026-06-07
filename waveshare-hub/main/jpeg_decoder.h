#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * Decode JPEG data to RGB565 buffer.
 * @param jpeg_data  Input JPEG bytes
 * @param jpeg_len   Length of JPEG data
 * @param rgb_out    Output RGB565 buffer (must be pre-allocated, CAM_FRAME_W * CAM_FRAME_H * 2)
 * @param out_w      Output: decoded width
 * @param out_h      Output: decoded height
 * @return true on success
 */
bool jpeg_decode_to_rgb565(const uint8_t *jpeg_data, int jpeg_len,
                            uint16_t *rgb_out, int *out_w, int *out_h);

#ifdef __cplusplus
}
#endif
