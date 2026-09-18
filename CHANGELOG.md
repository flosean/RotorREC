# Changelog

## Unreleased

- Translate all tracked documentation, source comments, and remaining localized diagnostic and camera mode strings into English.
- Record the project owner's confirmation that Betaflight UART communication and OSD functionality have been tested on real hardware and work correctly.
- Keep firmware version 1.0.0; this update changes documentation and displayed text, not control behavior.

## 1.0.0 - 2026-09-18

Initial source release under the RotorREC name.

- Separate ESP32-C3-Zero and ESP32-C6-LCD-1.47 builds sharing camera and Betaflight logic.
- DJI Action 2 legacy BLE/DUML and DJI public R SDK protocol paths.
- Pairing, saved camera profiles, reconnection, recording control, and camera status reporting.
- Betaflight MSPv2, USER1-USER4 control, two OSD lines, and loss-of-link control policy.
- LCD, BOOT button, C3 RGB indicator, and USB status logs.
- Host tests, board defaults, and setup and architecture documentation.
- Firmware version 1.0.0 from `version.txt`; application output named `rotorrec.bin`.
- Exclude build caches, captures, historical snapshots, downloaded references, and unfinished PCB drafts.

Remaining checks include Action 4 regression, repeated reconnection stress tests, and visual confirmation of the revised C3 LED colors. GoPro is not implemented. The project owner subsequently confirmed successful Betaflight UART and OSD hardware testing; see [verification status](docs/verification-status.md).
