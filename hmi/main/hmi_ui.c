/**
 * @file hmi_ui.c
 * @brief Portable LVGL motor-control HMI (no OS / IDF dependencies).
 */

#include "hmi_ui.h"

#include <stdint.h>

static lv_obj_t *s_target_speed_label;
static lv_obj_t *s_motor_status_label;
static lv_obj_t *s_bist_runtime_status_label;
static lv_obj_t *s_bist_postboot_status_label;
static lv_obj_t *s_overspeed_label;

static hmi_ui_ops_t s_ops;
static uint16_t s_target_speed = 1000;

static void power_button_event_cb(lv_event_t *event)
{
    const bool power_on = (bool)(uintptr_t)lv_event_get_user_data(event);

    if (s_ops.on_power) {
        s_ops.on_power(power_on, s_ops.user_data);
    }
}

static void speed_button_event_cb(lv_event_t *event)
{
    const int32_t direction = (int32_t)(intptr_t)lv_event_get_user_data(event);

    if (direction > 0 && s_target_speed <= UINT16_MAX - 100) {
        s_target_speed += 100;
    } else if (direction < 0 && s_target_speed >= 100) {
        s_target_speed -= 100;
    } else {
        return;
    }

    hmi_ui_set_target_speed(s_target_speed);

    if (s_ops.on_speed) {
        s_ops.on_speed(s_target_speed, s_ops.user_data);
    }
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, uint32_t color,
                               lv_event_cb_t event_cb, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_height(button, LV_PCT(100));
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

void hmi_ui_create(lv_obj_t *screen, const hmi_ui_ops_t *ops)
{
    if (ops != NULL) {
        s_ops = *ops;
        if (ops->initial_target_speed != 0) {
            s_target_speed = ops->initial_target_speed;
        }
    } else {
        s_ops = (hmi_ui_ops_t){0};
    }

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_pad_all(screen, 20, 0);
    lv_obj_set_style_pad_gap(screen, 16, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "MOTOR CONTROL HMI (BIST)");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8EEF4), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *power_row = lv_obj_create(screen);
    lv_obj_remove_style_all(power_row);
    lv_obj_set_width(power_row, LV_PCT(100));
    lv_obj_set_height(power_row, 90);
    lv_obj_set_style_pad_gap(power_row, 16, 0);
    lv_obj_set_flex_flow(power_row, LV_FLEX_FLOW_ROW);
    create_button(power_row, "ON", 0x168A45, power_button_event_cb, (void *)(uintptr_t)1);
    create_button(power_row, "OFF", 0xB3261E, power_button_event_cb, (void *)(uintptr_t)0);

    lv_obj_t *speed_panel = lv_obj_create(screen);
    lv_obj_set_width(speed_panel, LV_PCT(100));
    lv_obj_set_height(speed_panel, 150);
    lv_obj_set_style_bg_color(speed_panel, lv_color_hex(0x1B2A34), 0);
    lv_obj_set_style_border_width(speed_panel, 0, 0);
    lv_obj_set_style_radius(speed_panel, 12, 0);
    lv_obj_set_style_pad_all(speed_panel, 12, 0);
    lv_obj_set_style_pad_gap(speed_panel, 8, 0);
    lv_obj_set_flex_flow(speed_panel, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *speed_title = lv_label_create(speed_panel);
    lv_label_set_text(speed_title, "TARGET SPEED (100 RPM STEPS)");
    lv_obj_set_width(speed_title, LV_PCT(100));
    lv_obj_set_style_text_color(speed_title, lv_color_hex(0xAFC6D4), 0);
    lv_obj_set_style_text_align(speed_title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *selector_row = lv_obj_create(speed_panel);
    lv_obj_remove_style_all(selector_row);
    lv_obj_set_width(selector_row, LV_PCT(100));
    lv_obj_set_flex_grow(selector_row, 1);
    lv_obj_set_style_pad_gap(selector_row, 10, 0);
    lv_obj_set_flex_flow(selector_row, LV_FLEX_FLOW_ROW);

    create_button(selector_row, "-", 0x315B72, speed_button_event_cb, (void *)(intptr_t)-1);
    s_target_speed_label = lv_label_create(selector_row);
    lv_label_set_text_fmt(s_target_speed_label, "%u RPM", (unsigned)s_target_speed);
    lv_obj_set_flex_grow(s_target_speed_label, 2);
    lv_obj_set_height(s_target_speed_label, LV_PCT(100));
    lv_obj_set_style_text_color(s_target_speed_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_target_speed_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_target_speed_label, 22, 0);
    create_button(selector_row, "+", 0x315B72, speed_button_event_cb, (void *)(intptr_t)1);

    s_motor_status_label = lv_label_create(screen);
    lv_label_set_text(s_motor_status_label, "ACTUAL SPEED: -- RPM\nMOTOR TEMPERATURE: -- C");
    lv_obj_set_width(s_motor_status_label, LV_PCT(100));
    lv_obj_set_flex_grow(s_motor_status_label, 1);
    lv_obj_set_style_bg_color(s_motor_status_label, lv_color_hex(0x1B2A34), 0);
    lv_obj_set_style_bg_opa(s_motor_status_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_motor_status_label, 12, 0);
    lv_obj_set_style_pad_all(s_motor_status_label, 20, 0);
    lv_obj_set_style_text_color(s_motor_status_label, lv_color_hex(0xE8EEF4), 0);
    lv_obj_set_style_text_align(s_motor_status_label, LV_TEXT_ALIGN_CENTER, 0);

    s_bist_postboot_status_label = lv_label_create(screen);
    lv_label_set_text(s_bist_postboot_status_label, "BIST STATUS: POST-BOOT TESTS NOT STARTED");
    lv_obj_set_width(s_bist_postboot_status_label, LV_PCT(100));
    lv_obj_set_height(s_bist_postboot_status_label, 70);
    lv_obj_set_style_bg_color(s_bist_postboot_status_label, lv_color_hex(0x59636A), 0);
    lv_obj_set_style_bg_opa(s_bist_postboot_status_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_bist_postboot_status_label, 12, 0);
    lv_obj_set_style_pad_top(s_bist_postboot_status_label, 24, 0);
    lv_obj_set_style_text_color(s_bist_postboot_status_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_bist_postboot_status_label, LV_TEXT_ALIGN_CENTER, 0);

    s_bist_runtime_status_label = lv_label_create(screen);
    lv_label_set_text(s_bist_runtime_status_label, "BIST STATUS: RUNTIME TESTS NOT STARTED");
    lv_obj_set_width(s_bist_runtime_status_label, LV_PCT(100));
    lv_obj_set_height(s_bist_runtime_status_label, 70);
    lv_obj_set_style_bg_color(s_bist_runtime_status_label, lv_color_hex(0x59636A), 0);
    lv_obj_set_style_bg_opa(s_bist_runtime_status_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_bist_runtime_status_label, 12, 0);
    lv_obj_set_style_pad_top(s_bist_runtime_status_label, 24, 0);
    lv_obj_set_style_text_color(s_bist_runtime_status_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_bist_runtime_status_label, LV_TEXT_ALIGN_CENTER, 0);

    s_overspeed_label = lv_label_create(screen);
    lv_label_set_text(s_overspeed_label, "OVERSPEED ALARM: --");
    lv_obj_set_width(s_overspeed_label, LV_PCT(100));
    lv_obj_set_height(s_overspeed_label, 70);
    lv_obj_set_style_bg_color(s_overspeed_label, lv_color_hex(0x59636A), 0);
    lv_obj_set_style_bg_opa(s_overspeed_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_overspeed_label, 12, 0);
    lv_obj_set_style_pad_top(s_overspeed_label, 24, 0);
    lv_obj_set_style_text_color(s_overspeed_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_overspeed_label, LV_TEXT_ALIGN_CENTER, 0);
}

void hmi_ui_set_bist_postboot(bool ok)
{
    lv_label_set_text(s_bist_postboot_status_label,
                      ok ? "BIST STATUS: POST-BOOT TESTS OK"
                         : "BIST STATUS: POST-BOOT TESTS FAILED");
    lv_obj_set_style_bg_color(s_bist_postboot_status_label,
                              lv_color_hex(ok ? 0x168A45 : 0xC62828), 0);
}

void hmi_ui_set_bist_runtime(bool ok)
{
    lv_label_set_text(s_bist_runtime_status_label,
                      ok ? "BIST STATUS: RUNTIME TESTS OK"
                         : "BIST STATUS: RUNTIME TESTS FAILED");
    lv_obj_set_style_bg_color(s_bist_runtime_status_label,
                              lv_color_hex(ok ? 0x168A45 : 0xC62828), 0);
}

void hmi_ui_set_motor_status(uint16_t speed_rpm, uint16_t temperature_c)
{
    lv_label_set_text_fmt(s_motor_status_label,
                          "ACTUAL SPEED: %u RPM\nMOTOR TEMPERATURE: %u C",
                          (unsigned)speed_rpm, (unsigned)temperature_c);
}

void hmi_ui_set_motor_unavailable(void)
{
    lv_label_set_text(s_motor_status_label, "MOTOR COMMUNICATION UNAVAILABLE");
}

void hmi_ui_set_halted(void)
{
    lv_label_set_text(s_motor_status_label, "HMI STOPPED\nRUNTIME BIST FAILURE");
}

void hmi_ui_set_overspeed(bool active)
{
    lv_label_set_text(s_overspeed_label,
                      active ? "OVERSPEED ALARM: ACTIVE" : "OVERSPEED ALARM: CLEAR");
    lv_obj_set_style_bg_color(s_overspeed_label,
                              lv_color_hex(active ? 0xC62828 : 0x168A45), 0);
}

void hmi_ui_set_overspeed_unknown(void)
{
    lv_label_set_text(s_overspeed_label, "OVERSPEED ALARM: UNKNOWN");
    lv_obj_set_style_bg_color(s_overspeed_label, lv_color_hex(0x59636A), 0);
}

void hmi_ui_set_target_speed(uint16_t speed_rpm)
{
    lv_label_set_text_fmt(s_target_speed_label, "%u RPM", (unsigned)speed_rpm);
}
