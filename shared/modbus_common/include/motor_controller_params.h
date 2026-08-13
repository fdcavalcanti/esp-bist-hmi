/*
 * SPDX-FileCopyrightText: 2016-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*=====================================================================================
 * Description:
 *   Modbus parameter structures for the BIST HMI motor-controller map.
 *   Shared by the Tab5 master and the simulated slave.
 *====================================================================================*/
#ifndef _DEVICE_PARAMS
#define _DEVICE_PARAMS

#include <stdint.h>
#include "sdkconfig.h"

/* Reflects the Modbus address space for coils, discrete inputs, holding and input regs. */
#pragma pack(push, 1)
typedef struct {
    uint8_t overspeed_alarm: 1;
} discrete_reg_params_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint8_t motor_on_off: 1;
} coil_reg_params_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint16_t motor_current_speed;
    uint16_t motor_temperature;
} input_reg_params_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint16_t motor_speed_setpoint;
    uint16_t critical_speed;
} holding_reg_params_t;
#pragma pack(pop)

extern holding_reg_params_t holding_reg_params;
extern input_reg_params_t input_reg_params;
extern coil_reg_params_t coil_reg_params;
extern discrete_reg_params_t discrete_reg_params;

#endif /* !defined(_DEVICE_PARAMS) */
