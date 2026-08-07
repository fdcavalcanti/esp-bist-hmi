# BIST HMI

HMI demonstration on an **M5Stack Tab5** (ESP32-P4) that talks to a simulated motor controller over **Modbus RTU / RS-485**. The long-term goal is to run **ESP-BIST** on the Tab5 and show a safe industrial-style HMI on an Espressif SoC.

## Overview

| Role | Hardware | Firmware |
|------|----------|----------|
| Modbus master + touch HMI | M5Stack Tab5 (ESP32-P4) | [`hmi/`](hmi/) |
| Simulated motor controller (Modbus slave) | Separate ESP board + RS-485 transceiver | [`modbus_motor_controller/`](modbus_motor_controller/) |

```
┌─────────────────────┐         RS-485          ┌──────────────────────────┐
│  M5Stack Tab5       │◄───────────────────────►│  ESP motor controller    │
│  (Modbus master)    │      A / B + GND        │  (Modbus slave, addr 1)  │
│  LVGL touch UI      │                         │  Simulated drive params  │
└─────────────────────┘                         └──────────────────────────┘
```

Shared register definitions live in [`shared/modbus_common/`](shared/modbus_common/).

## Repository layout

```
bist_hmi/
├── hmi/                          # Tab5 HMI (Modbus master + LVGL UI)
│   ├── main/                     # bist_hmi.c, Tab5 display bring-up
│   └── components/
│       └── modbus_bist_master/   # Serial Modbus master API
├── modbus_motor_controller/      # Simulated motor controller (Modbus slave)
└── shared/
    └── modbus_common/            # Shared coil / register map
```

## Current status

**Working today**

- Tab5 LVGL UI: motor ON/OFF, target speed (±100 RPM), live speed / temperature / overspeed
- Modbus RTU master on the Tab5 (`modbus_bist_master`)
- Simulated motor controller slave on a second ESP over RS-485

**Planned**

- Integrate **ESP-BIST** on the Tab5
- Drive the on-screen **BIST STATUS** from real BIST results (UI placeholder: `BIST STATUS: NOT READY`)

## Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) **≥ 5.4** (HMI targets ESP32-P4 / Tab5)
- M5Stack Tab5
- Second ESP board for the motor controller slave (any target supported by that project)
- RS-485 transceivers (e.g. MAX485) on both ends, common GND

## Hardware / RS-485

Both sides need a UART ↔ RS-485 transceiver. Typical wiring:

```
         VCC ---------------+                               +--------------- VCC
                            |                               |
                    +-------x-------+               +-------x-------+
         RXD <------| RO            | DIFFERENTIAL  |             RO|-----> RXD
                    |              B|---------------|B              |
         TXD ------>| DI   MAX485   |    \  /       |    MAX485   DI|<----- TXD
Tab5 / ESP          |               |   RS-485 side |               |    Other ESP
         RTS --+--->| DE            |    /  \       |             DE|---+
               |    |              A|---------------|A              |   |
               +----| /RE           |    PAIR       |            /RE|---+-- RTS
                    +-------x--------+              +-------x-------+
                            |                               |
                           GND                             GND
```

**Tab5 Modbus UART defaults** (`hmi/sdkconfig.defaults`):

| Signal | GPIO |
|--------|------|
| RXD | 21 |
| TXD | 20 |
| RTS (DE/~RE) | 34 |
| Baud | 115200 |
| Mode | RTU |

Configure the motor controller UART pins in `idf.py menuconfig` → **Modbus Example Configuration** so they match your board and transceiver.

## Modbus register map

Defined in [`shared/modbus_common/include/motor_controller_params.h`](shared/modbus_common/include/motor_controller_params.h):

| Type | Field | Meaning |
|------|-------|---------|
| Coil | `motor_on_off` | Motor power |
| Discrete | `overspeed_alarm` | Overspeed alarm |
| Holding | `motor_speed_setpoint` | Target speed (RPM) |
| Holding | `critical_speed` | Critical speed threshold |
| Input | `motor_current_speed` | Actual speed (RPM) |
| Input | `motor_temperature` | Temperature (°C) |

Slave address defaults to **1**. Communication mode and baud must match on master and slave.

## Build and flash

### 1. Simulated motor controller

```bash
cd modbus_motor_controller
idf.py set-target <your-esp-target>
idf.py menuconfig   # set UART TX/RX/RTS for your RS-485 wiring
idf.py -p PORT flash monitor
```

See [`modbus_motor_controller/README.md`](modbus_motor_controller/README.md) for slave details.

### 2. Tab5 HMI

```bash
cd hmi
idf.py set-target esp32p4
idf.py -p PORT flash monitor
```

Component dependencies (Tab5 BSP, display) are pulled via the IDF Component Manager (`hmi/main/idf_component.yml`).

Modbus master API details: [`hmi/components/modbus_bist_master/README.md`](hmi/components/modbus_bist_master/README.md).

