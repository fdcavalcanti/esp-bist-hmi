#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TAB5_PANEL_UNKNOWN = 0,
    TAB5_PANEL_ILI9881C, /*!< Rev.1 board: ILI9881C panel, GT911 touch */
    TAB5_PANEL_ST7123,   /*!< Rev.2 board: ST7123 panel and touch (touch FW v3) */
    TAB5_PANEL_ST7121,   /*!< Rev.2 board: ST7121 panel, ST7123 touch (touch FW v1) */
} tab5_panel_type_t;

/**
 * @brief Detect the panel fitted to this Tab5 and bring up display, touch and LVGL.
 *
 * The Espressif BSP only supports ILI9881C and ST7123, so ST7121 boards are
 * initialized here instead. Backlight is left off; call bsp_display_backlight_on().
 *
 * @param[out] out_disp Resulting LVGL display, may be NULL
 */
esp_err_t tab5_display_start(lv_display_t **out_disp);

/** @brief Panel detected by tab5_display_start(). */
tab5_panel_type_t tab5_display_get_panel_type(void);

/** @brief Human readable name of the detected panel. */
const char *tab5_display_get_panel_name(void);

#ifdef __cplusplus
}
#endif
