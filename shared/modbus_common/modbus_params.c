/*
 * SPDX-FileCopyrightText: 2016-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Storage instances for the shared motor-controller Modbus map. */
#include <stdint.h>
#include "motor_controller_params.h"

holding_reg_params_t holding_reg_params = { 0 };

input_reg_params_t input_reg_params = { 0 };

coil_reg_params_t coil_reg_params = { 0 };

discrete_reg_params_t discrete_reg_params = { 0 };
