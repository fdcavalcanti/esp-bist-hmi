/*
 * SPDX-FileCopyrightText: 2016-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbcontroller.h"       // for mbcontroller defines and api
#include "motor_controller_params.h"      // for modbus parameters structures
#include "esp_log.h"            // for log_write
#include "sdkconfig.h"

#define MB_PORT_NUM     (CONFIG_MB_UART_PORT_NUM)   // Number of UART port used for Modbus connection
#define MB_SLAVE_ADDR   (CONFIG_MB_SLAVE_ADDR)      // The address of device in Modbus network
#define MB_DEV_SPEED    (CONFIG_MB_UART_BAUD_RATE)  // The communication speed of the UART

// Defines below are used to define register start address for each type of Modbus registers
#define HOLD_OFFSET(field) ((uint16_t)(offsetof(holding_reg_params_t, field) >> 1))
#define INPUT_OFFSET(field) ((uint16_t)(offsetof(input_reg_params_t, field) >> 1))
#define MB_REG_DISCRETE_INPUT_START         (0x0000)
#define MB_REG_COILS_START                  (0x0000)
#define MB_REG_INPUT_START_AREA0            (INPUT_OFFSET(motor_current_speed))
#define MB_REG_HOLDING_START_AREA0          (HOLD_OFFSET(motor_speed_setpoint))

#define MB_PAR_INFO_GET_TOUT                (10) // Timeout for get parameter info
#define MB_READ_MASK                        (MB_EVENT_INPUT_REG_RD \
                                                | MB_EVENT_HOLDING_REG_RD \
                                                | MB_EVENT_DISCRETE_RD \
                                                | MB_EVENT_COILS_RD)
#define MB_WRITE_MASK                       (MB_EVENT_HOLDING_REG_WR \
                                                | MB_EVENT_COILS_WR)
#define MB_READ_WRITE_MASK                  (MB_READ_MASK | MB_WRITE_MASK)
#define MB_CUST_DATA_MAX_LEN                (100)

#define MOTOR_UPDATE_PERIOD_MS              (1000)
#define MOTOR_SPEED_STEP_RPM                (100)
#define MOTOR_IDLE_TEMPERATURE_C            (25)
#define MOTOR_RPM_PER_DEGREE_C               (100)

static const char *TAG = "BIST_HMI_MOTOR_CONTROLLER";

static void *mbc_slave_handle = NULL;

#if CONFIG_FMB_CONTROLLER_SLAVE_ID_SUPPORT
#define MB_SLAVE_NAME_MAX_LEN 32
#define INIT_DEV_ID(struct_name, uid, running, serial, name) static struct {    \
        uint8_t slave_uid;                                                      \
        uint8_t is_running;                                                     \
        uint8_t length;                                                         \
        uint8_t marker;                                                         \
        uint32_t serial_number;                                                 \
        char dev_name[MB_SLAVE_NAME_MAX_LEN];                                   \
    } struct_name = {                                                           \
        .slave_uid = (uid),                                                     \
        .is_running = (running),                                                \
        .marker = 0x55,                                                         \
        .length = (sizeof(struct_name) - sizeof(struct_name.dev_name)           \
                            + strlen((name)) - 2),                              \
        .serial_number =(serial),                                               \
        .dev_name = name,                                                       \
    };
#endif

// Set register values into known state
static void setup_reg_data(void)
{
    discrete_reg_params.overspeed_alarm = 0;

    coil_reg_params.motor_on_off = 0;

    input_reg_params.motor_current_speed = 0;
    input_reg_params.motor_temperature = 25;

    holding_reg_params.motor_speed_setpoint = 0;
    holding_reg_params.critical_speed = 2000;
}

static void motor_update(void *arg)
{
    while (1) {
        uint16_t current_speed;
        uint16_t temperature;
        bool alarm;

        (void)mbc_slave_lock(mbc_slave_handle);

        uint16_t target_speed = coil_reg_params.motor_on_off
                                ? holding_reg_params.motor_speed_setpoint
                                : 0;

        if (input_reg_params.motor_current_speed < target_speed) {
            uint16_t difference = target_speed - input_reg_params.motor_current_speed;
            input_reg_params.motor_current_speed +=
                (difference < MOTOR_SPEED_STEP_RPM) ? difference : MOTOR_SPEED_STEP_RPM;
        } else if (input_reg_params.motor_current_speed > target_speed) {
            uint16_t difference = input_reg_params.motor_current_speed - target_speed;
            input_reg_params.motor_current_speed -=
                (difference < MOTOR_SPEED_STEP_RPM) ? difference : MOTOR_SPEED_STEP_RPM;
        }

        input_reg_params.motor_temperature =
            MOTOR_IDLE_TEMPERATURE_C +
            (input_reg_params.motor_current_speed / MOTOR_RPM_PER_DEGREE_C);
        discrete_reg_params.overspeed_alarm =
            input_reg_params.motor_current_speed > holding_reg_params.critical_speed;

        current_speed = input_reg_params.motor_current_speed;
        temperature = input_reg_params.motor_temperature;
        alarm = discrete_reg_params.overspeed_alarm;

        (void)mbc_slave_unlock(mbc_slave_handle);

        ESP_LOGI(TAG, "Motor update: speed=%" PRIu16 " RPM, temperature=%" PRIu16
                      " C, alarm=%s",
                 current_speed, temperature, alarm ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(MOTOR_UPDATE_PERIOD_MS));
    }
}

// This is a simple custom function handler for the command.
// The handler is executed from the context of modbus controller event task and should be as simple as possible.
// Parameters: frame_ptr - the pointer to the incoming ADU request frame from master starting from function code,
// len - the pointer to length of the frame. The handler body can override the buffer and return the length of data.
// After return from the handler the modbus object will handle the end of transaction according to the exception returned,
// then builds the response frame and send it back to the master. If the whole transaction time including the response
// latency exceeds the configured slave response time set in the master configuration the master will ignore the transaction.
mb_exception_t my_custom_fc_handler(void *inst, uint8_t *frame_ptr, uint16_t *len)
{
    char *str_append = ":Slave";
    MB_RETURN_ON_FALSE((frame_ptr && len && *len < (MB_CUST_DATA_MAX_LEN - strlen(str_append))), MB_EX_ILLEGAL_DATA_VALUE, TAG,
                       "incorrect custom frame");
    frame_ptr[*len] = '\0';
    strcat((char *)&frame_ptr[1], str_append);
    *len = (strlen(str_append) + *len); // the length of (response + command)
    return MB_EX_NONE; // Set the exception code for modbus object appropriately
}

// An example application of Modbus slave. It is based on esp-modbus stack.
// See deviceparams.h file for more information about assigned Modbus parameters.
// These parameters can be accessed from main application and also can be changed
// by external Modbus master host.
void app_main(void)
{
    mb_param_info_t reg_info; // keeps the Modbus registers access information
    mb_register_area_descriptor_t reg_area = {0}; // Modbus register area descriptor structure

    // Set UART log level
    esp_log_level_set(TAG, ESP_LOG_INFO);

#if !CONFIG_LOG_DEFAULT_LEVEL_DEBUG
    esp_log_level_set("mbc_serial.slave", ESP_LOG_DEBUG);
    esp_log_level_set("mb_object.slave", ESP_LOG_DEBUG);
#else
    // Disable VFS logs as they are too verbose
    esp_log_level_set("vfs_calls", ESP_LOG_NONE);
#endif

    // Initialize Modbus controller
    mb_communication_info_t comm_config = {
        .ser_opts.port = MB_PORT_NUM,
#if CONFIG_MB_COMM_MODE_ASCII
        .ser_opts.mode = MB_ASCII,
#elif CONFIG_MB_COMM_MODE_RTU
        .ser_opts.mode = MB_RTU,
#endif
        .ser_opts.baudrate = MB_DEV_SPEED,
        .ser_opts.parity = MB_PARITY_NONE,
        .ser_opts.uid = MB_SLAVE_ADDR,
        .ser_opts.data_bits = UART_DATA_8_BITS,
        .ser_opts.stop_bits = UART_STOP_BITS_1
    };

    ESP_ERROR_CHECK(mbc_slave_create_serial(&comm_config, &mbc_slave_handle)); // Initialization of Modbus controller

    const uint8_t custom_command = 0x41; // The custom command to be sent to slave
    // Try to delete the handler for specified command.
    esp_err_t err = mbc_delete_handler(mbc_slave_handle, custom_command);
    MB_RETURN_ON_FALSE((err == ESP_OK  || err == ESP_ERR_INVALID_STATE), ;, TAG,
                       "could not delete handler, returned (0x%x).", (int)err);
    err = mbc_set_handler(mbc_slave_handle, custom_command, my_custom_fc_handler);
    MB_RETURN_ON_FALSE((err == ESP_OK), ;, TAG,
                       "could not set or override handler, returned (0x%x).", (int)err);
    mb_fn_handler_fp handler = NULL;
    err = mbc_get_handler(mbc_slave_handle, custom_command, &handler);
    MB_RETURN_ON_FALSE((err == ESP_OK && handler == my_custom_fc_handler), ;, TAG,
                       "could not get handler for command %d, returned (0x%x).", (int)custom_command, (int)err);

    // The code below initializes Modbus register area descriptors
    // for Modbus Holding Registers, Input Registers, Coils and Discrete Inputs
    // Initialization should be done for each supported Modbus register area according to register map.
    // When external master trying to access the register in the area that is not initialized
    // by mbc_slave_set_descriptor() API call then Modbus stack
    // will send exception response for this register area.
    // Holding: motor_speed_setpoint, critical_speed
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = MB_REG_HOLDING_START_AREA0;
    reg_area.address = (void *)&holding_reg_params;
    reg_area.size = sizeof(holding_reg_params_t);
    reg_area.access = MB_ACCESS_RW;
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(mbc_slave_handle, reg_area));

    // Input: motor_current_speed, motor_temperature
    reg_area.type = MB_PARAM_INPUT;
    reg_area.start_offset = MB_REG_INPUT_START_AREA0;
    reg_area.address = (void *)&input_reg_params;
    reg_area.size = sizeof(input_reg_params_t);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(mbc_slave_handle, reg_area));

    // Coil: motor_on_off
    reg_area.type = MB_PARAM_COIL;
    reg_area.start_offset = MB_REG_COILS_START;
    reg_area.address = (void *)&coil_reg_params;
    reg_area.size = sizeof(coil_reg_params_t);
    reg_area.access = MB_ACCESS_RW;
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(mbc_slave_handle, reg_area));

    // Discrete: overspeed_alarm
    reg_area.type = MB_PARAM_DISCRETE;
    reg_area.start_offset = MB_REG_DISCRETE_INPUT_START;
    reg_area.address = (void *)&discrete_reg_params;
    reg_area.size = sizeof(discrete_reg_params_t);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(mbc_slave_handle, reg_area));

    setup_reg_data(); // Set values into known state

    // Set UART pin numbers
    ESP_ERROR_CHECK(uart_set_pin(MB_PORT_NUM, CONFIG_MB_UART_TXD,
                                 CONFIG_MB_UART_RXD, CONFIG_MB_UART_RTS,
                                 UART_PIN_NO_CHANGE));

    // Set UART driver mode to Half Duplex
    ESP_ERROR_CHECK(uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX));

    // Starts of modbus controller and stack
    err = mbc_slave_start(mbc_slave_handle);
    ESP_ERROR_CHECK(err);

    BaseType_t task_created = xTaskCreate(motor_update, "motor_update", 3072, NULL, 5, NULL);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

#if CONFIG_FMB_CONTROLLER_SLAVE_ID_SUPPORT
    // Initialize the new slave identificator structure (example)
    INIT_DEV_ID(new_id_struct, 0x00, 0x00, 0x11223344, "esp_modbus_serial_slave");
    uint8_t is_running = (bool)(err == ESP_OK);

    // This is the way to set Slave ID fields to retrieve it by master using report slave ID command.
    err = mbc_set_slave_id(mbc_slave_handle, comm_config.ser_opts.uid, is_running, &new_id_struct.length, new_id_struct.length);
    if (err == ESP_OK) {
        ESP_LOGW("SET_SLAVE_ID", "dev_name: %s", (char *)new_id_struct.dev_name);
        ESP_LOG_BUFFER_HEX_LEVEL("SET_SLAVE_ID", (void *)&new_id_struct.length, new_id_struct.length, ESP_LOG_WARN);
    } else {
        ESP_LOGE("SET_SLAVE_ID", "Set slave ID fail, err=%d.", err);
    }
#endif

    ESP_LOGI(TAG, "Modbus slave stack initialized.");
    ESP_LOGI(TAG, "Start motor controller slave...");

    while (1) {
        // Check for read/write events from Modbus master
        (void)mbc_slave_check_event(mbc_slave_handle, MB_READ_WRITE_MASK);
        ESP_ERROR_CHECK(mbc_slave_get_param_info(mbc_slave_handle, &reg_info, MB_PAR_INFO_GET_TOUT));
        const char *rw_str = (reg_info.type & MB_READ_MASK) ? "READ" : "WRITE";

        if (reg_info.type & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) {
            ESP_LOGI(TAG, "HOLDING %s (%" PRIu32 " us), ADDR:%u, SIZE:%u",
                     rw_str, reg_info.time_stamp,
                     (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
        } else if (reg_info.type & MB_EVENT_INPUT_REG_RD) {
            ESP_LOGI(TAG, "INPUT READ (%" PRIu32 " us), ADDR:%u, SIZE:%u",
                     reg_info.time_stamp,
                     (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
        } else if (reg_info.type & MB_EVENT_DISCRETE_RD) {
            ESP_LOGI(TAG, "DISCRETE READ (%" PRIu32 " us), ADDR:%u, SIZE:%u",
                     reg_info.time_stamp,
                     (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
        } else if (reg_info.type & (MB_EVENT_COILS_RD | MB_EVENT_COILS_WR)) {
            ESP_LOGI(TAG, "COILS %s (%" PRIu32 " us), ADDR:%u, SIZE:%u",
                     rw_str, reg_info.time_stamp,
                     (unsigned)reg_info.mb_offset, (unsigned)reg_info.size);
        }
    }
}
