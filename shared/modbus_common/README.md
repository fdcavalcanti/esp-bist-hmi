# modbus_common

Shared Modbus parameter structures for the BIST HMI demo. Both the Tab5 master and the motor-controller slave use this map so coil / register layouts stay in sync.

## Files

| File | Role |
|------|------|
| `include/motor_controller_params.h` | Coil, discrete, holding, and input register structs |
| `modbus_params.c` | Zero-initialized storage instances |

## Register map

| Type | Field | Meaning |
|------|-------|---------|
| Coil | `motor_on_off` | Motor power |
| Discrete | `overspeed_alarm` | Overspeed alarm |
| Holding | `motor_speed_setpoint` | Target speed (RPM) |
| Holding | `critical_speed` | Critical speed threshold |
| Input | `motor_current_speed` | Actual speed (RPM) |
| Input | `motor_temperature` | Temperature (°C) |

## How it is pulled in

Path dependencies in Component Manager manifests:

- `hmi/components/modbus_bist_master/idf_component.yml` → `../../../shared/modbus_common`
- `modbus_motor_controller/main/idf_component.yml` → `../../shared/modbus_common`
