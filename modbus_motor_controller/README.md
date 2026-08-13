# Modbus Motor Controller

Simulated motor drive for the BIST HMI demo. Runs as a **Modbus RTU slave** on a second ESP board and exposes the register map in [`shared/modbus_common`](../shared/modbus_common/).

The Tab5 HMI (`../hmi/`) is the Modbus master that reads/writes these parameters over RS-485.

## Behaviour

- Coil `motor_on_off` — motor power
- Holding `motor_speed_setpoint` / `critical_speed` — target and overspeed threshold
- Input `motor_current_speed` / `motor_temperature` — simulated actuals (ramps toward setpoint; temperature rises with speed)
- Discrete `overspeed_alarm` — set when actual speed exceeds critical speed

Slave address defaults to **1**. Mode and baud must match the Tab5 master (RTU @ 115200 by default).

## Hardware

UART ↔ RS-485 transceiver on both ends (see root [`README.md`](../README.md) for the wiring diagram). Common GND required.

## Configure

```bash
. "$IDF_PATH/export.sh"
cd modbus_motor_controller
idf.py set-target <your-esp-target>
idf.py menuconfig
```

Under **Motor Controller Modbus**, set UART port, baud, TX/RX/RTS pins, communication mode, and slave address for your board.

Default pin hints in Kconfig (override for your wiring):

| Signal | Typical ESP32 / C6 | Typical S2 / S3 / C3 / C2 / H2 | Transceiver |
|--------|--------------------|--------------------------------|-------------|
| TxD | GPIO23 | GPIO9 | DI |
| RxD | GPIO22 | GPIO8 | RO |
| RTS | GPIO18 | GPIO10 | DE / ~RE |

Each target has different available GPIOs — confirm against the UART docs for your chip.

## Build and flash

Requires ESP-IDF **6.0** (see `main/idf_component.yml`).

```bash
idf.py -p PORT flash monitor
```

(To exit the serial monitor, type `Ctrl-]`.)

## Related

- Shared map: [`shared/modbus_common`](../shared/modbus_common/)
- Tab5 master: [`hmi/components/modbus_bist_master`](../hmi/components/modbus_bist_master/)
