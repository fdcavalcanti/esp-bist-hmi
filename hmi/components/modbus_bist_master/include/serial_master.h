/*
 * SPDX-FileCopyrightText: 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Characteristic IDs for the motor controller Modbus map.
 *
 * Must stay in sync with the parameter descriptor table and
 * shared/modbus_common register layout.
 */
typedef enum {
    CID_MOTOR_ON_OFF = 0,
    CID_MOTOR_SPEED_SETPOINT,
    CID_CRITICAL_SPEED,
    CID_MOTOR_CURRENT_SPEED,
    CID_MOTOR_TEMPERATURE,
    CID_OVERSPEED_ALARM,
    CID_COUNT
} mb_cid_t;

/**
 * @brief Initialize Modbus serial master, UART pins, and start the stack.
 *
 * Uses Kconfig options under "Modbus Master Configuration"
 * (UART port, baud, TX/RX/RTS pins, RTU/ASCII).
 */
esp_err_t master_init(void);

/**
 * @brief Stop and destroy the Modbus master stack.
 */
esp_err_t master_destroy(void);

/**
 * @brief Read a characteristic from the Modbus network by CID.
 *
 * @param[in]  cid         Characteristic ID
 * @param[out] value       Buffer for the value (must be at least param_size bytes)
 * @param[in]  value_size  Size of @p value in bytes
 */
esp_err_t master_read(uint16_t cid, void *value, size_t value_size);

/**
 * @brief Write a characteristic to the Modbus network by CID.
 *
 * @param[in] cid         Characteristic ID
 * @param[in] value       Buffer with the value to write
 * @param[in] value_size  Size of @p value in bytes (must match param_size)
 */
esp_err_t master_write(uint16_t cid, const void *value, size_t value_size);

/** Convenience helpers for common register widths. */
esp_err_t master_read_u16(uint16_t cid, uint16_t *value);
esp_err_t master_write_u16(uint16_t cid, uint16_t value);
esp_err_t master_read_u8(uint16_t cid, uint8_t *value);
esp_err_t master_write_u8(uint16_t cid, uint8_t value);

#ifdef __cplusplus
}
#endif
