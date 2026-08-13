# Tab5 HMI

ESP32-P4 firmware for the M5Stack Tab5: **ESP-BIST Host Diagnostics** on the LP core, LVGL motor-control UI, and Modbus RTU master toward the simulated drive.

## Role of each source

| File | Responsibility |
|------|----------------|
| `bist_hmi.c` | `app_main`: LP bring-up, HD agent, post-boot gate, Modbus + UI loop, safe-state on BIST fail |
| `hmi_ui.c` / `.h` | Portable LVGL UI (no IDF calls); motor controls + BIST status labels |
| `tab5_display.c` / `.h` | Detect panel (ILI9881C / ST7123 / ST7121) and start display + touch + LVGL |
| `ulp/main.c` | LP companion loop (`bist_hd_companion_*`) |

## Boot sequence

1. Initialize LP UART and load/run the ULP binary (unless waking from ULP).
2. Start the Host Diagnostic Agent and a status task that waits for post-boot `LP_STATUS`.
3. After post-boot completes, bring up the display and UI.
4. If post-boot failed, stop — Modbus motor control never starts.
5. Otherwise initialize Modbus, poll the slave, and mirror runtime BIST into the UI.
6. On runtime BIST failure: cut motor power, clear the command queue, show halted UI.

## Build

Requires ESP-IDF **6.0**, a sibling `esp-bist` tree, and the ULP linker patch. Full steps are in the [root README](../README.md).

```bash
. "$IDF_PATH/export.sh"
cd hmi
idf.py set-target esp32p4
idf.py -p PORT flash monitor
```

Console uses **USB Serial/JTAG**. First build fetches managed components listed in `main/idf_component.yml`.

### Notable `sdkconfig.defaults`

- Tab5 flash / SPIRAM / cache / P4 revision (&lt; v3)
- Modbus master UART: RX 21, TX 20, RTS 34, RTU 115200
- ULP LP-core + ESP-BIST / Host Diagnostics options

### CMake notes

- `EXTRA_COMPONENT_DIRS` points at `../../esp-bist` (sibling of this repo).
- `main/ulp/CMakeLists.txt` links `esp-bist/src/bist` and applies the BIST LP linker script via `ulp_apply_custom_linker_script()` (needs the IDF patch).
- On IDF 6.0, the project CMake applies an `esp_lvgl_port` MIPI DSI callback compatibility patch.

## UI

- Motor ON / OFF
- Target speed in **100 RPM** steps (default **1000**)
- Live actual speed, temperature, overspeed
- Separate labels for post-boot and runtime BIST status

## Related

- Master API: [`components/modbus_bist_master/README.md`](components/modbus_bist_master/README.md)
- Shared map: [`../shared/modbus_common/README.md`](../shared/modbus_common/README.md)
- ESP-BIST IDF sample (Host Diagnostics background): `esp-bist/samples/idf/README.md`
