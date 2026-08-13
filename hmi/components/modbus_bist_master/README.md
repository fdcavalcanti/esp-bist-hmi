# Modbus BIST Master Component

Serial Modbus master for the Tab5 HMI. Talks to `modbus_motor_controller`
using the shared register map in `shared/modbus_common`.

This repo pins ESP-IDF **6.0** for the HMI application; the component itself
depends on `espressif/esp-modbus` ^2.

## Public API (`serial_master.h`)

| Function | Purpose |
|---|---|
| `master_init()` / `master_destroy()` | Start / tear down the Modbus stack |
| `master_read()` / `master_write()` | Generic CID read/write on the network |
| `master_read_u16()` / `master_write_u16()` | Typed helpers |
| `master_read_u8()` / `master_write_u8()` | Typed helpers |

CIDs (`mb_cid_t`): motor on/off, speed setpoint, critical speed, current speed, temperature, overspeed alarm.

## Usage

```c
#include "serial_master.h"

ESP_ERROR_CHECK(master_init());

uint16_t speed = 0;
ESP_ERROR_CHECK(master_read_u16(CID_MOTOR_CURRENT_SPEED, &speed));

ESP_ERROR_CHECK(master_write_u8(CID_MOTOR_ON_OFF, 1));
ESP_ERROR_CHECK(master_write_u16(CID_MOTOR_SPEED_SETPOINT, 1000));
```

UART port, baud, and pins are configured under **Modbus Master Configuration**
in `menuconfig` (defaults for Tab5 are in `hmi/sdkconfig.defaults`).
