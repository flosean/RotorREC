# RotorREC verification status

Updated: 2026-10-02. Firmware version: 1.2.0-dev (unreleased; new hardware acceptance pending).

**The project owner confirms that Betaflight UART communication and OSD functionality have been tested on real hardware, used successfully, and work correctly.** This replaces the earlier pending-hardware status for UART and OSD. The confirmation did not include a specific FC firmware/board combination or a detailed cycle-count and fault-injection record.

## 1.2 development software verification

On 2026-10-02:

- Implemented first-batch CamLink-inspired settings, two/four-line OSD, per-field DJI telemetry validity, optional low-battery warning and persistent Link Pause. Usage and physical acceptance are in [camera settings](camera-settings.md).
- ESP-IDF v5.5.5 builds pass for both unified board images. C3 app: 903,616 / 2,031,616-byte slot (56% free); C6 app: 1,290,528 / 4,128,768-byte slot (69% free). These are image/partition figures, not runtime heap measurements.
- Betaflight host suite passes 11,956 checks, including wire formatting, four-line boundaries, unknown/zero/stale fields, warning hysteresis and maximum thresholds, settings validation, paused OSD and LED brightness. This count includes loops and is not a count of independent scenarios.
- Actual DJI R SDK adapter callbacks pass native tests for video/photo/unknown modes, seconds/MB conversion, parameter validity, malformed/maximum-length extension fields, stale/disconnected data, command invalidation, UINT32_MAX sentinels and tick wrap. Transport, clock and locks are substituted; no BLE radio is exercised.
- GoPro packet/status/advertisement and Betaflight integration suite passes.
- Actual management receiver passes native NVS/OTA fault injection, including new defaults/invalid storage, immutable current vs saved settings, commit failure, replayed saves, paused pairing rejection, BOOT resume and offline recovery with stale recording/saving flags. Storage/OTA are simulated; physical power-cut behavior remains unverified.
- Fifteen Python manager tests pass for wire/settings/package/reboot handling and existing update retries. One hidden Tk regression passes using ESP-IDF Python 3.12, Tk and pyserial: connection fields stay locked through queued replies; successful status populates settings; changing connections requires a fresh read; worker exceptions restore controls. No real serial device is opened. The system Python lacks pyserial; the GUI regression used the existing configured runtime.
- Versioned update ZIPs were generated for C3/C6, archive contents/hash/board/version were read back and compared with the final binaries. No hardware was flashed.

### Standards

Review covered the uncommitted implementation against baseline `83c3bf1`, repository architecture and the code-review smell baseline. One confirmed issue: GUI connection edits during a worker could apply an old status reply to a new connection. Connection/settings controls now remain locked until the FIFO completion event; the hidden Tk regression covers success and exception restoration. No remaining confirmed standards defects were found; no hard architecture violations were reported.

### Spec

Review used the first-batch scope in [CamLink evaluation](camlink-evaluation.md). One confirmed issue: stale recording/saving flags after a complete disconnect could block Link Pause/reboot. The busy gate now requires a live connection for those flags, still rejects pending commands and live unknown/recording/saving states, and passes offline-recovery regression. No remaining confirmed spec defects were found. Optional Wi-Fi/ARM/new-model features remain deferred.

Review totals: Standards 1 finding resolved, worst was the GUI connection race; Spec 1 finding resolved, worst was blocked offline recovery. New features still require both-board BLE/OSD/power-cycle/update acceptance; the earlier owner's UART/OSD confirmation does not establish 1.2 validation.

## 1.1 software verification

- Unified C3-Zero and C6-LCD builds pass with two OTA slots and rollback enabled. C3 app: 900,528 bytes / 2,031,616-byte slot; C6 app: 1,287,296 bytes / 4,128,768-byte slot.
- Link-time RAM usage: C3 124,506 / 321,296 bytes; C6 199,520 / 452,112 bytes. These are static linker figures, not free heap measured during BLE operation.
- Matching update ZIPs and segmented USB-install ZIPs were generated in each `build-passthrough-*` directory and checked for archive integrity; update manifests/hashes were read back and matched to the final app binaries.
- Tk GUI construction passes a hidden-window smoke check using the existing ESP-IDF Python runtime (pyserial 3.5, Tk 8.6); interactive device operation is not covered.
- Existing Betaflight host suite: 10,971 checks pass; GoPro protocol/integration host suite passes.
- Actual management receiver tested with NVS/OTA fault injection and native SHA-256; framing includes one million noise bytes and a CRC32 known vector.
- Ten Python manager checks pass, including full mocked update/reboot with a dropped DATA ACK, same-version rollback detection, corrupted package rejection and cancellation.
- These are software tests. Real passthrough, runtime camera switching, GUI interaction, Flash power-cut recovery, RAM headroom under BLE load and bootloader rollback remain **pending on both boards**. See [passthrough acceptance](passthrough.md).

## Hardware evidence (earlier firmware)

| Area | Confirmed | Remaining checks |
| --- | --- | --- |
| Action 2 | Initial pairing, start/stop, actual recording state, battery, Bluetooth icon, and keepalive | Long-duration and repeated regression |
| Action 2 reconnection | Individual camera/module restarts and recovery after going out of range during recording; recording can be stopped after recovery | Ten restarts per device, 20 repeated recording cycles, recording-file integrity |
| Action 4 | Pairing, recording control, and settings retrieval in an earlier version | Full current modular-version regression |
| C6 LCD | Orientation, offset, buffer, and SPI initialization tested previously | Current-version hardware regression |
| C3-Zero | Flash verification, Action 2 session, standby and battery reception; UART TX0/RX1 confirmed in logs | Visual confirmation of corrected RGB colors; full button and reconnection regression |
| Betaflight UART and OSD | Project owner reports successful real-hardware testing and use; host tests also cover MSPv2, USER control, OSD, and loss-of-link policy | Repeated stress and fault-injection coverage beyond the reported functional test |
| GoPro | Software implementation and host protocol tests only | Pairing, start/stop, battery, keepalive, reboot/range recovery, BOOT and BF integration on a specific model/firmware |

## Known limitations

- When DJI is selected, without a saved camera, an unsuccessful initial search requires a BOOT hold or RESET. Saved targets reconnect automatically. GoPro repeats discovery automatically.
- Action 2 recording duration and remaining capacity are outside the current delivery scope. Unverified fields are not treated as valid data.
- Action 2 pairing retains fixed fields from an existing remote flow; behavior across multiple cameras has not been established.
- C3 uses RGB byte order based on observed color reversal. Other boards may differ; host tests cannot verify visible color.
- The FC may retain the last OSD text after module power loss or UART disconnection. See [Betaflight setup](betaflight-setup.md).
- GoPro uses saved runtime protocol selection and requires Video mode. No GoPro model is hardware-verified yet. See [GoPro setup](gopro.md) for protocol scope and acceptance checks.

## Initial release software checks

On 2026-09-18, the tracked source was copied to a separate clean directory without local sdkconfig files, downloaded components, or existing build caches:

- MSVC host tests passed 10,971 checks; this is not a count of independent scenarios.
- ESP-IDF v5.5.5 builds passed for ESP32-C3 and ESP32-C6.
- Both build descriptions reported project_name=rotorrec and project_version=1.0.0.
- Initial C3 application: 864,960 bytes, 44% partition space free. Initial C6 application: 1,257,120 bytes, 18% free. These sizes describe the original release before the English string update.
- The automated build session did not flash hardware. The owner's UART/OSD hardware confirmation is recorded separately above.

Acceptance procedures are in [requirements](requirements.md); build instructions are in [building and flashing](build-and-flash.md). Historical raw records and binary snapshots remain local and are excluded from Git.

## GoPro integration software checks

On 2026-09-19:

- GoPro host tests passed: packet reassembly, malformed/truncated replies, continuation order/wrap, status validation, advertisement filtering and shared Betaflight policy/OSD.
- The existing Betaflight suite passed all 10,971 checks.
- ESP-IDF v5.5.5 builds passed for all four board/protocol combinations: C3 GoPro, C6 GoPro, C3 DJI and C6 DJI.
- GoPro application sizes: C3 804,864 bytes (48% partition space free); C6 1,198,304 bytes (22% free).
- No hardware was flashed or tested during this integration. BLE pairing, radio timing and actual recording remain pending hardware acceptance.

## English documentation and text update

On 2026-09-18, all tracked files passed a scan for Chinese characters and all local Markdown links resolved. A source-token comparison confirmed that C/C++ code changed only in comments and explicitly translated display/log strings. The 10,971 host checks and both C3/C6 builds passed again. Firmware version remains 1.0.0; no hardware was flashed during this text update.
