# RotorREC verification status

Updated: 2026-09-18. Firmware version: 1.0.0.

**The project owner confirms that Betaflight UART communication and OSD functionality have been tested on real hardware, used successfully, and work correctly.** This replaces the earlier pending-hardware status for UART and OSD. The confirmation did not include a specific FC firmware/board combination or a detailed cycle-count and fault-injection record.

## Hardware evidence

| Area | Confirmed | Remaining checks |
| --- | --- | --- |
| Action 2 | Initial pairing, start/stop, actual recording state, battery, Bluetooth icon, and keepalive | Long-duration and repeated regression |
| Action 2 reconnection | Individual camera/module restarts and recovery after going out of range during recording; recording can be stopped after recovery | Ten restarts per device, 20 repeated recording cycles, recording-file integrity |
| Action 4 | Pairing, recording control, and settings retrieval in an earlier version | Full current modular-version regression |
| C6 LCD | Orientation, offset, buffer, and SPI initialization tested previously | Current-version hardware regression |
| C3-Zero | Flash verification, Action 2 session, standby and battery reception; UART TX0/RX1 confirmed in logs | Visual confirmation of corrected RGB colors; full button and reconnection regression |
| Betaflight UART and OSD | Project owner reports successful real-hardware testing and use; host tests also cover MSPv2, USER control, OSD, and loss-of-link policy | Repeated stress and fault-injection coverage beyond the reported functional test |

## Known limitations

- Without a saved camera, an unsuccessful initial search requires a BOOT hold or RESET. Saved targets reconnect automatically.
- Action 2 recording duration and remaining capacity are outside the current delivery scope. Unverified fields are not treated as valid data.
- Action 2 pairing retains fixed fields from an existing remote flow; behavior across multiple cameras has not been established.
- C3 uses RGB byte order based on observed color reversal. Other boards may differ; host tests cannot verify visible color.
- The FC may retain the last OSD text after module power loss or UART disconnection. See [Betaflight setup](betaflight-setup.md).
- GoPro is not implemented.

## Initial release software checks

On 2026-09-18, the tracked source was copied to a separate clean directory without local sdkconfig files, downloaded components, or existing build caches:

- MSVC host tests passed 10,971 checks; this is not a count of independent scenarios.
- ESP-IDF v5.5.5 builds passed for ESP32-C3 and ESP32-C6.
- Both build descriptions reported project_name=rotorrec and project_version=1.0.0.
- Initial C3 application: 864,960 bytes, 44% partition space free. Initial C6 application: 1,257,120 bytes, 18% free. These sizes describe the original release before the English string update.
- The automated build session did not flash hardware. The owner's UART/OSD hardware confirmation is recorded separately above.

Acceptance procedures are in [requirements](requirements.md); build instructions are in [building and flashing](build-and-flash.md). Historical raw records and binary snapshots remain local and are excluded from Git.

## English documentation and text update

On 2026-09-18, all tracked files passed a scan for Chinese characters and all local Markdown links resolved. A source-token comparison confirmed that C/C++ code changed only in comments and explicitly translated display/log strings. The 10,971 host checks and both C3/C6 builds passed again. Firmware version remains 1.0.0; no hardware was flashed during this text update.
