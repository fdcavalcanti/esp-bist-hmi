/*
 * SPDX-FileCopyrightText: 2016-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbcontroller.h"
#include "motor_controller_params.h"
#include "sdkconfig.h"
#include "serial_master.h"

#define MB_PORT_NUM     (CONFIG_MB_UART_PORT_NUM)
#define MB_DEV_SPEED    (CONFIG_MB_UART_BAUD_RATE)

/* Instance offset macros (+1 because 0 means "unused" in the descriptor) */
#define HOLD_OFFSET(field)  ((uint16_t)(offsetof(holding_reg_params_t, field) + 1))
#define INPUT_OFFSET(field) ((uint16_t)(offsetof(input_reg_params_t, field) + 1))
#define COIL_INST_OFFSET    (1)
#define DISCR_INST_OFFSET   (1)

#define STR(fieldname) ((const char *)(fieldname))
#define HOLD_REG_START(field)  (HOLD_OFFSET(field) >> 1)
#define HOLD_REG_SIZE(field)   (sizeof(((holding_reg_params_t *)0)->field) >> 1)
#define INPUT_REG_START(field) (INPUT_OFFSET(field) >> 1)
#define INPUT_REG_SIZE(field)  (sizeof(((input_reg_params_t *)0)->field) >> 1)

#define MOTOR_SPEED_MIN  (0)
#define MOTOR_SPEED_MAX  (3000)
#define MOTOR_TEMP_MIN   (0)
#define MOTOR_TEMP_MAX   (150)

#define OPTS(min_val, max_val, step_val) { .opt1 = min_val, .opt2 = max_val, .opt3 = step_val }

#define MB_CUST_DATA_LEN 100

static const char *TAG = "MB_BIST_MASTER";

enum {
    MB_DEVICE_ADDR1 = 1
};

/* Data dictionary: mirrors shared/modbus_common and the motor controller slave map. */
static const mb_parameter_descriptor_t device_parameters[] = {
    {
        CID_MOTOR_ON_OFF, STR("Motor_On_Off"), STR("On/Off"), MB_DEVICE_ADDR1, MB_PARAM_COIL,
        0, 1,
        COIL_INST_OFFSET, PARAM_TYPE_U8, 1,
        OPTS(0, 1, 0), PAR_PERMS_READ_WRITE_TRIGGER
    },
    {
        CID_MOTOR_SPEED_SETPOINT, STR("Motor_Speed_Setpoint"), STR("RPM"), MB_DEVICE_ADDR1, MB_PARAM_HOLDING,
        HOLD_REG_START(motor_speed_setpoint), HOLD_REG_SIZE(motor_speed_setpoint),
        HOLD_OFFSET(motor_speed_setpoint), PARAM_TYPE_U16, 2,
        OPTS(MOTOR_SPEED_MIN, MOTOR_SPEED_MAX, 0), PAR_PERMS_READ_WRITE_TRIGGER
    },
    {
        CID_CRITICAL_SPEED, STR("Critical_Speed"), STR("RPM"), MB_DEVICE_ADDR1, MB_PARAM_HOLDING,
        HOLD_REG_START(critical_speed), HOLD_REG_SIZE(critical_speed),
        HOLD_OFFSET(critical_speed), PARAM_TYPE_U16, 2,
        OPTS(MOTOR_SPEED_MIN, MOTOR_SPEED_MAX, 0), PAR_PERMS_READ_WRITE_TRIGGER
    },
    {
        CID_MOTOR_CURRENT_SPEED, STR("Motor_Current_Speed"), STR("RPM"), MB_DEVICE_ADDR1, MB_PARAM_INPUT,
        INPUT_REG_START(motor_current_speed), INPUT_REG_SIZE(motor_current_speed),
        INPUT_OFFSET(motor_current_speed), PARAM_TYPE_U16, 2,
        OPTS(MOTOR_SPEED_MIN, MOTOR_SPEED_MAX, 0), PAR_PERMS_READ
    },
    {
        CID_MOTOR_TEMPERATURE, STR("Motor_Temperature"), STR("C"), MB_DEVICE_ADDR1, MB_PARAM_INPUT,
        INPUT_REG_START(motor_temperature), INPUT_REG_SIZE(motor_temperature),
        INPUT_OFFSET(motor_temperature), PARAM_TYPE_U16, 2,
        OPTS(MOTOR_TEMP_MIN, MOTOR_TEMP_MAX, 0), PAR_PERMS_READ
    },
    {
        CID_OVERSPEED_ALARM, STR("Overspeed_Alarm"), STR("On/Off"), MB_DEVICE_ADDR1, MB_PARAM_DISCRETE,
        0, 1,
        DISCR_INST_OFFSET, PARAM_TYPE_U8, 1,
        OPTS(0, 1, 0), PAR_PERMS_READ
    },
};

static const uint16_t num_device_parameters =
    (uint16_t)(sizeof(device_parameters) / sizeof(device_parameters[0]));

static char s_custom_data[MB_CUST_DATA_LEN] = {0};
static void *s_master_handle = NULL;

static mb_exception_t custom_handler(void *inst, uint8_t *frame_ptr, uint16_t *len)
{
    (void)inst;
    MB_RETURN_ON_FALSE((frame_ptr && len && *len && *len < (MB_CUST_DATA_LEN - 1)),
                       MB_EX_ILLEGAL_DATA_VALUE, TAG, "incorrect custom frame buffer");
    ESP_LOGD(TAG, "Custom handler, Frame ptr: %p, len: %u", frame_ptr, *len);
    strncpy(&s_custom_data[0], (char *)&frame_ptr[1], MB_CUST_DATA_LEN - 1);
    s_custom_data[MB_CUST_DATA_LEN - 1] = '\0';
    ESP_LOG_BUFFER_HEXDUMP("CUSTOM_DATA", &s_custom_data[0], (*len - 1), ESP_LOG_DEBUG);
    return MB_EX_NONE;
}

static esp_err_t ensure_master(void)
{
    if (s_master_handle == NULL) {
        ESP_LOGE(TAG, "Modbus master is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t get_cid_info(uint16_t cid, const mb_parameter_descriptor_t **param_descriptor)
{
    esp_err_t err = ensure_master();
    if (err != ESP_OK) {
        return err;
    }
    if (param_descriptor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = mbc_master_get_cid_info(s_master_handle, cid, param_descriptor);
    if ((err == ESP_ERR_NOT_FOUND) || (*param_descriptor == NULL)) {
        ESP_LOGE(TAG, "CID #%u not found in descriptor", (unsigned)cid);
        return ESP_ERR_NOT_FOUND;
    }
    return err;
}

esp_err_t master_read(uint16_t cid, void *value, size_t value_size)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const mb_parameter_descriptor_t *desc = NULL;
    esp_err_t err = get_cid_info(cid, &desc);
    if (err != ESP_OK) {
        return err;
    }
    if (value_size < desc->param_size) {
        ESP_LOGE(TAG, "CID #%u read buffer too small (%u < %u)",
                 (unsigned)cid, (unsigned)value_size, (unsigned)desc->param_size);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t type = 0;
    err = mbc_master_get_parameter(s_master_handle, cid, (uint8_t *)value, &type);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CID #%u (%s) read fail, err = 0x%x (%s)",
                 (unsigned)cid, (char *)desc->param_key, (int)err, esp_err_to_name(err));
    }
    return err;
}

esp_err_t master_write(uint16_t cid, const void *value, size_t value_size)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const mb_parameter_descriptor_t *desc = NULL;
    esp_err_t err = get_cid_info(cid, &desc);
    if (err != ESP_OK) {
        return err;
    }
    if (value_size != desc->param_size) {
        ESP_LOGE(TAG, "CID #%u write size mismatch (%u != %u)",
                 (unsigned)cid, (unsigned)value_size, (unsigned)desc->param_size);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t type = 0;
    err = mbc_master_set_parameter(s_master_handle, cid, (uint8_t *)value, &type);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CID #%u (%s) write fail, err = 0x%x (%s)",
                 (unsigned)cid, (char *)desc->param_key, (int)err, esp_err_to_name(err));
    }
    return err;
}

esp_err_t master_read_u16(uint16_t cid, uint16_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return master_read(cid, value, sizeof(*value));
}

esp_err_t master_write_u16(uint16_t cid, uint16_t value)
{
    return master_write(cid, &value, sizeof(value));
}

esp_err_t master_read_u8(uint16_t cid, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return master_read(cid, value, sizeof(*value));
}

esp_err_t master_write_u8(uint16_t cid, uint8_t value)
{
    return master_write(cid, &value, sizeof(value));
}

esp_err_t master_init(void)
{
    if (s_master_handle != NULL) {
        ESP_LOGW(TAG, "Modbus master already initialized");
        return ESP_OK;
    }

    mb_communication_info_t comm = {
        .ser_opts.port = MB_PORT_NUM,
#if CONFIG_MB_COMM_MODE_ASCII
        .ser_opts.mode = MB_ASCII,
#elif CONFIG_MB_COMM_MODE_RTU
        .ser_opts.mode = MB_RTU,
#endif
        .ser_opts.baudrate = MB_DEV_SPEED,
        .ser_opts.parity = MB_PARITY_NONE,
        .ser_opts.uid = 0,
        .ser_opts.response_tout_ms = 1000,
        .ser_opts.data_bits = UART_DATA_8_BITS,
        .ser_opts.stop_bits = UART_STOP_BITS_1
    };

    esp_err_t err = mbc_master_create_serial(&comm, &s_master_handle);
    MB_RETURN_ON_FALSE((s_master_handle != NULL), ESP_ERR_INVALID_STATE, TAG,
                       "mb controller initialization fail.");
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "mb controller initialization fail, returns(0x%x).", (int)err);

    const uint8_t override_command = 0x41;
    err = mbc_delete_handler(s_master_handle, override_command);
    MB_RETURN_ON_FALSE((err == ESP_OK || err == ESP_ERR_INVALID_STATE), ESP_ERR_INVALID_STATE, TAG,
                       "could not override handler, returned (0x%x).", (int)err);
    err = mbc_set_handler(s_master_handle, override_command, custom_handler);
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "could not override handler, returned (0x%x).", (int)err);

    err = uart_set_pin(MB_PORT_NUM, CONFIG_MB_UART_TXD, CONFIG_MB_UART_RXD,
                       CONFIG_MB_UART_RTS, UART_PIN_NO_CHANGE);
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "mb serial set pin failure, uart_set_pin() returned (0x%x).", (int)err);

    err = mbc_master_set_descriptor(s_master_handle, &device_parameters[0], num_device_parameters);
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "mb controller set descriptor fail, returns(0x%x).", (int)err);

    err = uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "mb serial set mode failure, uart_set_mode() returned (0x%x).", (int)err);

    /* Let the RS485 transceiver settle before the first transaction. */
    vTaskDelay(pdMS_TO_TICKS(50));

    err = mbc_master_start(s_master_handle);
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "mb controller start fail, returned (0x%x).", (int)err);

    ESP_LOGI(TAG, "Modbus master stack initialized");
    return ESP_OK;
}

esp_err_t master_destroy(void)
{
    if (s_master_handle == NULL) {
        return ESP_OK;
    }

    esp_err_t err = mbc_master_delete(s_master_handle);
    s_master_handle = NULL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_master_delete failed, err = 0x%x (%s)", (int)err, esp_err_to_name(err));
    }
    return err;
}
