#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_master.h"

static const char *TAG = "bist_hmi";

static void log_motor_status(void)
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
        return;
    }

    ESP_LOGI(TAG, "Motor status: on=%u speed=%u RPM temp=%u C overspeed=%u",
             (unsigned)motor_on,
             (unsigned)current_speed,
             (unsigned)temperature,
             (unsigned)overspeed_alarm);
}

void app_main(void)
{
    esp_err_t err = ESP_FAIL;
    ESP_LOGI(TAG, "Starting BIST HMI");
    ESP_ERROR_CHECK(master_init());

    ESP_LOGI(TAG, "Enabling motor");

    err = master_write_u8(CID_MOTOR_ON_OFF, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Motor did not respond. Check RS485 wiring (A/B, GND), "
                      "slave power, address and baud rate.");
        return;
    }

    ESP_LOGI(TAG, "Setting speed to 1000 RPM");
    err = master_write_u16(CID_MOTOR_SPEED_SETPOINT, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set speed");
        return;
    }

    for (;;) {
        log_motor_status();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

}
