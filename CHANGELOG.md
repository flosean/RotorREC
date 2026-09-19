# Changelog

## 1.1.0 - Unreleased

- Add a runtime-selectable official Open GoPro BLE adapter: bonding, session setup, saved-target reconnection, keepalive, video start/stop and actual encoding/battery status. Connect it to BOOT, Betaflight and both board interfaces; hardware validation is pending.
- Add GoPro protocol/bridge host tests, unified board builds and pairing instructions.
- Translate all tracked documentation, source comments, and remaining localized diagnostic and camera mode strings into English.
- Record the project owner's confirmation that Betaflight UART communication and OSD functionality have been tested on real hardware and work correctly.
- Add the Betaflight passthrough GUI/CLI, saved camera selection, bounded management framing, dual OTA slots, SHA-256/image checks, replay protection and startup rollback. Existing installations need one USB migration.
- Add management fault-injection tests and host update/reboot tests. Physical passthrough, camera switching and power-cut acceptance remain pending.

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
