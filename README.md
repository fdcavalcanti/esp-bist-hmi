# BIST HMI

HMI demonstration on an **M5Stack Tab5** (ESP32-P4) that runs **ESP-BIST** Host Diagnostics on the LP core and talks to a simulated motor controller over **Modbus RTU / RS-485**.

## Overview

| Role | Hardware | Firmware |
|------|----------|----------|
| Modbus master + touch HMI + BIST agent | M5Stack Tab5 (ESP32-P4) | [`hmi/`](hmi/) |
| Simulated motor controller (Modbus slave) | Separate ESP board + RS-485 transceiver | [`modbus_motor_controller/`](modbus_motor_controller/) |

```
┌─────────────────────┐         RS-485          ┌──────────────────────────┐
│  M5Stack Tab5       │◄───────────────────────►│  ESP motor controller    │
│  (Modbus master)    │      A / B + GND        │  (Modbus slave, addr 1)  │
│  LVGL + ESP-BIST    │                         │  Simulated drive params  │
└─────────────────────┘                         └──────────────────────────┘
```

Shared register definitions live in [`shared/modbus_common/`](shared/modbus_common/).

## Workspace layout

This project expects a sibling **esp-bist** checkout (paths in `hmi/CMakeLists.txt` and `hmi/main/ulp/CMakeLists.txt`):

```
esp_m5_workspace/          # or any parent folder
├── bist_hmi/              # this repository
├── esp-bist/              # required sibling
└── esp-idf-v6.0/          # ESP-IDF 6.0 (or export IDF_PATH)
```

```
bist_hmi/
├── hmi/                          # Tab5 HMI (BIST + Modbus master + LVGL)
│   ├── main/
│   │   ├── bist_hmi.c            # app_main: HD agent, UI, Modbus loop
│   │   ├── hmi_ui.c / .h         # LVGL motor + BIST status UI
│   │   ├── tab5_display.c / .h   # panel detect (ILI9881C / ST7123 / ST7121)
│   │   └── ulp/                  # LP companion (ESP-BIST Host Diagnostics)
│   └── components/
│       └── modbus_bist_master/   # Serial Modbus master API
├── modbus_motor_controller/      # Simulated motor controller (Modbus slave)
└── shared/
    └── modbus_common/            # Shared coil / register map
```

## Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) **6.0** (pinned in component manifests)
- Sibling [esp-bist](https://github.com/espressif/esp-bist) checkout (see layout above)
- ULP custom linker patch applied to your IDF install (see below)
- M5Stack Tab5
- Second ESP board for the motor controller slave
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

Configure the motor controller UART pins in `idf.py menuconfig` → **Motor Controller Modbus** so they match your board and transceiver.

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

### 0. One-time setup

```bash
# Clone esp-bist next to this repo if needed
# git clone https://github.com/espressif/esp-bist.git

# Export ESP-IDF 6.0
. "$IDF_PATH/export.sh"

# Apply the ULP custom linker patch (required for ESP-BIST LP builds)
cd "$IDF_PATH"
git apply /path/to/esp-bist/samples/idf/patches/idf_ulp_linker.patch
```

The patch adds `ulp_apply_custom_linker_script()`, which the HMI ULP project uses instead of the stock LP linker script.

### 1. Simulated motor controller

```bash
cd bist_hmi/modbus_motor_controller
idf.py set-target <your-esp-target>   # e.g. esp32s3
idf.py menuconfig                     # Motor Controller Modbus → UART TX/RX/RTS
idf.py -p PORT flash monitor
```

Defaults: RTU, 115200, slave address **1**. See [`modbus_motor_controller/README.md`](modbus_motor_controller/README.md).

### 2. Tab5 HMI

```bash
cd bist_hmi/hmi
idf.py set-target esp32p4
idf.py -p PORT flash monitor
```

Notes:

- First build pulls managed components (`m5stack_tab5`, `esp_lvgl_port` 2.9.0, `esp_lcd_st7121`).
- On IDF 6.0, `hmi/CMakeLists.txt` applies a small `esp_lvgl_port` MIPI DSI compatibility patch automatically.
- Console is **USB Serial/JTAG** — use the Tab5 USB-Serial/JTAG port.
- Tab5 flash / SPIRAM / P4 revision defaults are in `hmi/sdkconfig.defaults`.

More detail: [`hmi/README.md`](hmi/README.md).  
Modbus master API: [`hmi/components/modbus_bist_master/README.md`](hmi/components/modbus_bist_master/README.md).

## Runtime behaviour

1. LP companion starts; HP Host Diagnostic Agent waits for post-boot BIST.
2. Display comes up after post-boot completes. If post-boot fails, motor control does not start.
3. Modbus master polls the slave and drives the UI.
4. On runtime BIST failure, the HMI cuts motor power and shows a halted state.
