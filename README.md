# RotorREC

**ESP32-based action camera control for Betaflight, with recording status and battery feedback on OSD.**

RotorREC connects an action camera to your FPV setup. An ESP32 communicates with the camera over BLE, reads a Betaflight USER mode over UART/MSPv2, controls recording, and sends actual camera status and battery level to the OSD.

Current firmware version: **1.0.0**. **Betaflight UART communication and OSD functionality have been tested on real hardware and confirmed working by the project owner.**

## Features

- DJI Action 2 legacy BLE/DUML: pairing, saved camera profiles, automatic reconnection, recording control, actual recording state, and battery level.
- DJI public R SDK: a separate protocol path for Action 4; connection, recording control, and recording settings were verified in an earlier hardware revision.
- Betaflight USER1-USER4 recording control. USER1 is the default; assign the AUX channel and switch range in Betaflight Modes.
- OSD Custom Message 1 for camera status and Custom Message 2 for camera battery level.
- Short-press BOOT to toggle recording; hold for 1.2 seconds to scan and pair again.
- ESP32-C3-Zero RGB status indicator or ESP32-C6-LCD-1.47 display interface.

## Support status

| Component | Status |
| --- | --- |
| DJI Action 2 | Pairing, recording, and status reporting verified; individual restart and out-of-range recovery tests passed; repeated stress testing remains open |
| DJI Action 4 | Connection, recording, and settings retrieval verified previously; hardware regression of the current modular version remains open |
| ESP32-C3-Zero | Flashed and verified to receive Action 2 status and battery data; visual confirmation of the revised LED colors and full operational regression remain open |
| ESP32-C6-LCD-1.47 | LCD verified previously; current-version hardware regression remains open |
| Betaflight UART and OSD | Tested on real hardware and confirmed working by the project owner |
| GoPro | Planned; not implemented |

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
idf.py -B build-c3-zero -DIDF_TARGET=esp32c3 -DSDKCONFIG=build-c3-zero/sdkconfig -DSDKCONFIG_DEFAULTS=config/sdkconfig.c3-zero.defaults build
idf.py -B build-c3-zero -p PORT flash monitor
```

ESP32-C6-LCD-1.47, with LVGL downloaded on the first build:

```sh
idf.py -B build-modular -DIDF_TARGET=esp32c6 build
idf.py -B build-modular -p PORT flash monitor
```

Replace `PORT` with your serial port. Use separate build directories for the two boards and flash only the image for your board. See [building and flashing](docs/build-and-flash.md) for PowerShell wrappers and tool requirements.

## Project layout

```text
main/
  app_main.c       Startup and main loop
  camera/          Shared camera state and control; separate DJI protocol adapters
  betaflight/      MSPv2, USER mode policy, UART, and OSD
  platform/        Button, LCD, and RGB hardware interfaces
  ui/              Display, headless logging, and status indication
components/        Required DJI and Waveshare components
config/            C3 board defaults
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
```

Tests cover MSP framing and parsing, USER control policy, OSD text, and LED state and color serialization. The initial release passed 10,971 checks and clean builds for both boards. Hardware verification is tracked separately.

## Documentation and licensing

- [Documentation index](docs/README.md)
- [Changelog](CHANGELOG.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

A project-wide license has not yet been selected for the original code. Third-party components retain their own notices and terms. RotorREC is an independent project.
