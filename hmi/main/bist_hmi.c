#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "serial_master.h"
#include "tab5_display.h"

static const char *TAG = "bist_hmi";

static lv_display_t *s_disp;
static lv_obj_t *s_target_speed_label;
static lv_obj_t *s_motor_status_label;
static lv_obj_t *s_overspeed_label;
static QueueHandle_t s_command_queue;
static uint16_t s_target_speed = 1000;

typedef enum {
    MOTOR_COMMAND_SET_POWER,
    MOTOR_COMMAND_SET_SPEED,
} motor_command_type_t;

typedef struct {
    motor_command_type_t type;
    uint16_t value;
} motor_command_t;

static void queue_motor_command(motor_command_type_t type, uint16_t value)
{
    const motor_command_t command = {
        .type = type,
        .value = value,
    };

    if (xQueueSend(s_command_queue, &command, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Motor command queue is full");
    }
}

static void power_button_event_cb(lv_event_t *event)
{
    const uint16_t power_on = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
    queue_motor_command(MOTOR_COMMAND_SET_POWER, power_on);
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

    lv_label_set_text_fmt(s_target_speed_label, "%u RPM", (unsigned)s_target_speed);
    queue_motor_command(MOTOR_COMMAND_SET_SPEED, s_target_speed);
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

static void create_ui(lv_obj_t *screen)
{
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

    lv_obj_t *bist_status_label = lv_label_create(screen);
    lv_label_set_text(bist_status_label, "BIST STATUS: NOT READY");
    lv_obj_set_width(bist_status_label, LV_PCT(100));
    lv_obj_set_height(bist_status_label, 70);
    lv_obj_set_style_bg_color(bist_status_label, lv_color_hex(0xC62828), 0);
    lv_obj_set_style_bg_opa(bist_status_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bist_status_label, 12, 0);
    lv_obj_set_style_pad_top(bist_status_label, 24, 0);
    lv_obj_set_style_text_color(bist_status_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(bist_status_label, LV_TEXT_ALIGN_CENTER, 0);

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

static esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing Tab5 display");
    ESP_RETURN_ON_ERROR(tab5_display_start(&s_disp), TAG, "Display bring-up failed");

    bsp_display_rotate(s_disp, LV_DISPLAY_ROTATION_90);
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    bsp_display_lock(0);
    lv_obj_t *scr = lv_display_get_screen_active(s_disp);
    create_ui(scr);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Display ready");
    return ESP_OK;
}

static void update_motor_status(void)
{
    uint8_t motor_on = 0;
    uint16_t current_speed = 0;
    uint16_t temperature = 0;
    uint8_t overspeed_alarm = 0;

    if (master_read_u8(CID_MOTOR_ON_OFF, &motor_on) != ESP_OK ||
        master_read_u16(CID_MOTOR_CURRENT_SPEED, &current_speed) != ESP_OK ||
        master_read_u16(CID_MOTOR_TEMPERATURE, &temperature) != ESP_OK ||
        master_read_u8(CID_OVERSPEED_ALARM, &overspeed_alarm) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read complete motor status");
        bsp_display_lock(0);
        lv_label_set_text(s_motor_status_label, "MOTOR COMMUNICATION UNAVAILABLE");
        lv_label_set_text(s_overspeed_label, "OVERSPEED ALARM: UNKNOWN");
        lv_obj_set_style_bg_color(s_overspeed_label, lv_color_hex(0x59636A), 0);
        bsp_display_unlock();
        return;
    }

    ESP_LOGI(TAG, "Motor status: on=%u speed=%u RPM temp=%u C overspeed=%u",
             (unsigned)motor_on,
             (unsigned)current_speed,
             (unsigned)temperature,
             (unsigned)overspeed_alarm);

    bsp_display_lock(0);
    lv_label_set_text_fmt(s_motor_status_label,
                          "ACTUAL SPEED: %u RPM\nMOTOR TEMPERATURE: %u C",
                          (unsigned)current_speed, (unsigned)temperature);
    lv_label_set_text(s_overspeed_label,
                      overspeed_alarm ? "OVERSPEED ALARM: ACTIVE" : "OVERSPEED ALARM: CLEAR");
    lv_obj_set_style_bg_color(s_overspeed_label,
                              lv_color_hex(overspeed_alarm ? 0xC62828 : 0x168A45), 0);
    bsp_display_unlock();
}

static void execute_motor_command(const motor_command_t *command)
{
    esp_err_t err;

    if (command->type == MOTOR_COMMAND_SET_POWER) {
        err = master_write_u8(CID_MOTOR_ON_OFF, (uint8_t)command->value);
        ESP_LOGI(TAG, "Motor power command: %s", command->value ? "ON" : "OFF");
    } else {
        err = master_write_u16(CID_MOTOR_SPEED_SETPOINT, command->value);
        ESP_LOGI(TAG, "Motor target speed command: %u RPM", (unsigned)command->value);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Motor command failed: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting BIST HMI");

    s_command_queue = xQueueCreate(8, sizeof(motor_command_t));
    ESP_ERROR_CHECK(s_command_queue ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(master_init());

    TickType_t last_status_update = 0;
    for (;;) {
        motor_command_t command;
        if (xQueueReceive(s_command_queue, &command, pdMS_TO_TICKS(100)) == pdTRUE) {
            execute_motor_command(&command);
        }

        if (xTaskGetTickCount() - last_status_update >= pdMS_TO_TICKS(1000)) {
            update_motor_status();
            last_status_update = xTaskGetTickCount();
        }
    }
}
