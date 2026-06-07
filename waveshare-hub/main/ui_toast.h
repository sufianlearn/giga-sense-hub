#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Toast notification overlay — shows transient messages
 *        at the bottom of the screen with auto-dismiss.
 */

typedef enum {
    TOAST_INFO = 0,
    TOAST_WARNING,
    TOAST_ERROR,
    TOAST_SUCCESS,
} toast_level_t;

/* Show a toast notification. Duration in ms (0 = 3000ms default).
   Must be called with LVGL mutex held. */
void ui_toast_show(const char *msg, toast_level_t level, uint32_t duration_ms);

/* Init (call once after lv_init) */
void ui_toast_init(void);

#ifdef __cplusplus
}
#endif
