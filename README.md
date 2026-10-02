# RotorREC

**ESP32-based action camera control for Betaflight, with recording status and battery feedback on OSD.**

RotorREC connects an action camera to your FPV setup. An ESP32 communicates with the camera over BLE, reads a Betaflight USER mode over UART/MSPv2, controls recording, and sends actual camera status and battery level to the OSD.

Current firmware version: **1.2.0-dev (new features pending hardware validation)**. **Earlier Betaflight UART communication and OSD functionality have been tested on real hardware and confirmed working by the project owner.**

## Features

- Betaflight passthrough manager: select Action 2 / DJI R SDK / GoPro, pair cameras, and update over the existing UART using dual app slots and rollback. See [passthrough setup](docs/passthrough.md); new functionality is not yet hardware-validated.
- Saved USER1-USER4 selection, two/four OSD lines, C3 LED brightness and optional low-battery warning. Four-line DJI R SDK telemetry uses individually validated camera fields. See [camera settings](docs/camera-settings.md).
- Persistent Link Pause retains pairing, disables BLE and clears the owned camera OSD lines. Resume from the manager or by holding BOOT for 1.2 seconds.
- DJI Action 2 legacy BLE/DUML: pairing, saved camera profiles, automatic reconnection, recording control, actual recording state, and battery level.
- DJI public R SDK: a separate protocol path for Action 4; connection, recording control, and recording settings were verified in an earlier hardware revision.
- GoPro official Open GoPro BLE: runtime-selectable pairing, saved target, reconnection, video start/stop, actual encoding status, battery, and keepalive. Hardware validation is pending; see [GoPro setup](docs/gopro.md).
- Betaflight USER1-USER4 recording control. USER1 is the default; assign the AUX channel and switch range in Betaflight Modes.
- OSD Custom Messages 1/2 for camera status/battery by default; optional 3/4 for recording settings/storage. Action 2 and GoPro extra fields remain unavailable.
- Short-press BOOT to toggle recording; hold for 1.2 seconds to scan and pair again, or resume when Link Pause is active.
- ESP32-C3-Zero RGB status indicator or ESP32-C6-LCD-1.47 display interface.

## Support status

| Component | Status |
| --- | --- |
| DJI Action 2 | Pairing, recording, and status reporting verified; individual restart and out-of-range recovery tests passed; repeated stress testing remains open |
| DJI Action 4 | Connection, recording, and settings retrieval verified previously; hardware regression of the current modular version remains open |
| ESP32-C3-Zero | Flashed and verified to receive Action 2 status and battery data; visual confirmation of the revised LED colors and full operational regression remain open |
| ESP32-C6-LCD-1.47 | LCD verified previously; current-version hardware regression remains open |
| Betaflight UART and OSD | Tested on real hardware and confirmed working by the project owner |
| GoPro | Official Open GoPro BLE adapter implemented; hardware compatibility and functional validation pending |
| 1.2 settings, four-line OSD, warnings and Link Pause | Implemented; host tests and both board builds pass; current-version hardware acceptance remains pending |

See [verification status](docs/verification-status.md) for evidence and remaining checks.

## Hardware and wiring

| Board | ESP32 TX to FC RX | ESP32 RX to FC TX | Local interface |
| --- | --- | --- | --- |
| Waveshare ESP32-C3-Zero (4 MB) | GP0 | GP1 | BOOT, GPIO10 RGB, USB logs |
| Waveshare ESP32-C6-LCD-1.47 (8 MB) | GPIO18 | GPIO19 | BOOT, LCD, USB logs |

Connect a common ground. UART uses **3.3 V logic, 115200 baud, 8N1**. Power the board from USB for initial testing; never connect battery voltage or 5 V to a GPIO.

The integration targets **Betaflight 2025.12 or newer, with MSP API 1.47 or newer**. Enable MSP on an unused UART, assign an AUX switch to USER1, and enable both OSD Custom Message elements. Remove propellers for bench testing and move the switch to its inactive position before enabling control. See [Betaflight setup](docs/betaflight-setup.md).

## Quick start

Use **ESP-IDF v5.5.5**. Open an initialized ESP-IDF terminal, then clone the repository:

```sh
git clone https://github.com/flosean/RotorREC.git
cd RotorREC
```

ESP32-C3-Zero, without a display or downloaded component dependencies:

```sh
idf.py -B build-passthrough-c3 -DIDF_TARGET=esp32c3 -DSDKCONFIG=build-passthrough-c3/sdkconfig -DSDKCONFIG_DEFAULTS=config/sdkconfig.c3-zero.defaults build
idf.py -B build-passthrough-c3 -p PORT flash monitor
```

ESP32-C6-LCD-1.47, with LVGL downloaded on the first build:

```sh
idf.py -B build-passthrough-c6 -DIDF_TARGET=esp32c6 -DSDKCONFIG=build-passthrough-c6/sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build-passthrough-c6 -p PORT flash monitor
```

Replace `PORT` with your serial port. Use separate build directories for the two boards and flash only the image for your board. See [building and flashing](docs/build-and-flash.md) for PowerShell wrappers and tool requirements.

Action 2 is the default. Select DJI R SDK or GoPro with the [passthrough manager](docs/passthrough.md); changing the selection restarts the ESP. Version 1.0 installations require one USB installation of the new bootloader, partition table and application before passthrough updates work.

## Camera settings and Link Pause

The manager requires Python 3.10+ with pyserial 3.5; the GUI also uses tkinter:

```sh
python -m pip install pyserial==3.5
python tools/rotorrec_manager.py gui
```

Connect to the FC USB port, close Betaflight Configurator, and choose **Read status** before editing settings. **Save settings & restart** saves USER1-USER4, two/four OSD lines, C3 LED brightness (1-100%) and the camera low-battery threshold (0=off). Defaults are two OSD lines, 5% LED brightness and warnings off. Configure the matching USER mode and Custom Message elements in Betaflight.

Four-line mode adds confirmed DJI R SDK recording/remaining time, resolution/FPS and capacity. Unknown or stale fields show `--`; Action 2 and GoPro extra telemetry remains unavailable. After four-line mode has been enabled, switching back to two lines clears Messages 3/4; the module retains ownership of those lines.

**Pause camera link** saves Link Pause and restarts without initializing BLE. Pairing is retained across power cycles. Resume in the manager or hold BOOT for 1.2 seconds. Settings changes are rejected while the connected camera is recording, saving, awaiting a command or has unconfirmed recording status. After maintenance, power-cycle the **FC** to leave passthrough and restore OSD/USER control.

See [camera settings and acceptance](docs/camera-settings.md) for CLI commands and behavior, and [passthrough setup](docs/passthrough.md) for firmware update packages. Existing 1.1 dual-slot installations can update to 1.2 without another layout migration.

## Project layout

```text
main/
  app_main.c       Startup and main loop
  camera/          Shared camera state and control; separate DJI and GoPro adapters
  betaflight/      MSPv2, USER mode policy, UART, and OSD
  management/      Framing, saved settings, camera selection, UART update receiver
  platform/        Button, LCD, and RGB hardware interfaces
  ui/              Display, headless logging, and status indication
components/        Required DJI and Waveshare components
config/            Board and GoPro protocol defaults
tests/             Host tests for protocol, control policy, and status indication
tools/             Build and test entry points
docs/              Setup, architecture, requirements, and verification
version.txt        Firmware version source
```

Camera protocols and Betaflight interact through the shared camera controller. UI code does not access BLE directly. See [architecture](docs/architecture.md).

Only current source, tests, and documentation are tracked. Build output, packet captures, historical snapshots, downloaded research material, unfinished PCB drafts, and personal settings stay outside Git.

## Tests

On Windows, install Visual Studio C++ Build Tools and run:

```powershell
.\tools\test-betaflight.ps1
.\tools\test-gopro.ps1
.\tools\test-dji.ps1
.\tools\test-management.ps1
python tests/test_manager_gui.py
```

Tests cover MSP framing, USER policy, two/four-line OSD, LED brightness, actual DJI telemetry callbacks and management storage/update failures. The GUI test requires Python with tkinter and pyserial. Current results and hardware acceptance are tracked in [verification status](docs/verification-status.md).

On 2026-10-02, 11,956 Betaflight checks, 15 Python manager tests, the hidden Tk GUI regression, DJI callback tests and GoPro/management suites passed. Both ESP-IDF v5.5.5 board builds passed. These software results do not establish BLE radio behavior, physical OSD output or power-cut recovery of the new features.

## Documentation and licensing

- [Documentation index](docs/README.md)
- [Changelog](CHANGELOG.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

A project-wide license has not yet been selected for the original code. Third-party components retain their own notices and terms. RotorREC is an independent project.
