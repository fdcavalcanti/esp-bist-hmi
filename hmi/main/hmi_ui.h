/**
 * @file hmi_ui.h
 * @brief Portable motor-control HMI built with LVGL only (no OS / IDF APIs).
 *
 * Callers must hold their platform LVGL lock around create/update entry points.
 * Requires LVGL 9.x.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hmi_ui_power_cb_t)(bool power_on, void *user_data);
typedef void (*hmi_ui_speed_cb_t)(uint16_t target_speed_rpm, void *user_data);

typedef struct {
    hmi_ui_power_cb_t on_power;
    hmi_ui_speed_cb_t on_speed;
    void *user_data;
    uint16_t initial_target_speed;
} hmi_ui_ops_t;

void hmi_ui_create(lv_obj_t *screen, const hmi_ui_ops_t *ops);

void hmi_ui_set_bist_postboot(bool ok);
void hmi_ui_set_bist_runtime(bool ok);

void hmi_ui_set_motor_status(uint16_t speed_rpm, uint16_t temperature_c);
void hmi_ui_set_motor_unavailable(void);
void hmi_ui_set_halted(void);

void hmi_ui_set_overspeed(bool active);
void hmi_ui_set_overspeed_unknown(void);
void hmi_ui_set_target_speed(uint16_t speed_rpm);

#ifdef __cplusplus
}
#endif
