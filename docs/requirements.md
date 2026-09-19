# RotorREC 1.1 additions

Passthrough settings and update requirements are specified in [passthrough setup](passthrough.md). Runtime selection supersedes any build-time camera choice below. All new hardware behavior remains pending validation.

# RotorREC requirements

Updated: 2026-09-18.

Action 2 scope: connection and reconnection, actual recording state, start/stop control, and battery level. Recording duration and storage-capacity research are deferred and do not block this release.

Boards: ESP32-C6-LCD-1.47 with a display, and ESP32-C3-Zero with 4 MB flash, BOOT, USB logs, BF OSD, and RGB indication. Camera and BF logic are shared; sdkconfig, build directories, and UART defaults are separate. C3 uses GP0 TX, GP1 RX, and GPIO10 for the WS2812.

## Product goal

Connect a Betaflight flight controller to an action camera through an ESP32. Control recording from an AUX-assigned USER mode and display camera status through Betaflight Custom Messages. LCD and BOOT provide local operation and diagnostics.

## Camera scope

### DJI

Keep two separate protocol paths:

1. **Public R SDK:** Action 4 and cameras implementing the public protocol. Only documented hardware results count as verified support.
2. **Action 2 legacy DUML:** Action 2 does not use the current public R SDK handshake and requires the verified legacy frames.

Do not share pairing frames or assume identical status formats simply because both paths target DJI cameras.

### GoPro

Use only the official public Open GoPro BLE specification. The runtime-selected adapter implements bonding, discovery, session setup, reconnect, keepalive, explicit video recording control and camera-reported encoding/battery status. Require Open GoPro API 2.x and the documented command/query/management characteristics. Do not claim hardware support until a model and firmware pass the checks in [GoPro setup](gopro.md).

### Out of scope

- Blackmagic, Sony, and other camera brands.
- Simultaneous control of multiple Action 2 cameras.
- Native mobile applications. Prefer an onboard Wi-Fi web interface if configuration UI is needed later.
- Changing flight-controller motor, RC, PID, or other flight-safety settings.

## Pairing and reconnection

### Action 2

- Allow initial confirmation on the camera.
- Save the paired BLE MAC and address type in NVS.
- After either device restarts, reconnect to the saved camera and restore the session without another manual confirmation.
- After unexpected loss, scan for the saved camera approximately once per second without power-saving backoff. Stop scanning when found; retain connection and discovery timeouts.
- Restore legacy notifications, session, and keepalive before accepting recording requests.
- Holding BOOT for 1.2 seconds cancels reconnection and starts fresh discovery/pairing.
- Confirm that accepted pairing displays the Bluetooth icon on the camera.
- Legacy pairing retains fixed fields from an official remote flow, verified only with the current camera. Identify controller ID, serial, pairing code, and CRC fields before generalizing them.

### Public R SDK

- Discover the camera, connect over BLE/GATT, and perform public-protocol verification.
- Display the four-digit pairing code on LCD or in headless USB logs; accept it on the camera.
- Subscribe to recording state, mode, settings, battery, recording duration, and remaining capacity/time where supported.
- Perform Action 4 hardware regression after refactoring; compilation alone is not acceptance.

## Local interface

- BOOT short press: toggle recording when ready.
- BOOT hold for 1.2 seconds: scan/pair again.
- RST: hardware reset only.
- C3 RGB: steady green for confirmed standby, slow green for recording, slow red for disconnected/failed search, fast red for discovery/pairing/reconnection, and fast yellow for unknown/pending/storage state. Unknown status must not appear green.
- LCD: connection stage, pairing code, protocol, reconnection, recording state, settings, battery, duration, and diagnostics.
- Show only confirmed Action 2 data; do not infer unparsed recording settings or battery values.

## Betaflight

UART and OSD functionality have been tested on hardware and confirmed working by the project owner. The following requirements remain the basis for implementation and regression checks.

- Target Betaflight 2025.12 or newer with MSP API 1.47 or newer.
- Use 3.3 V UART1, 115200 baud, 8N1; C6 defaults to GPIO18 TX/GPIO19 RX, C3 to GP0 TX/GP1 RX. Cross TX/RX with an unused FC MSP UART and share ground. Pins are configurable.
- Check `MSP_API_VERSION` before enabling automatic control.
- Read `MSP_FC_VARIANT`, `MSP_BOXIDS`, and `MSP_STATUS`. AUX assignment and ranges remain in BF Modes, without a fixed raw RC channel.
- Write native MSPv2 `MSP2_SET_TEXT (0x3007)`, types 7-10 for Custom Messages 1-4, at most 16 bytes per line.
- Use line 1 for actual recording/connection state and line 2 for battery; leave lines 3 and 4 untouched. Update on changes and MSP reconnection, and refresh RAM text every five seconds. Never send `MSP_EEPROM_WRITE`.
- Default to USER1 (permanent ID 40), configurable through USER4. Activation starts and deactivation stops recording; ARM is not used. Require a valid mode mapping, healthy FC state, ready camera, valid status, 200 ms debounce, and duplicate prevention.
- Link loss must not send stop. After startup or FC/receiver recovery, require an inactive switch position before new commands. See [Betaflight setup](betaflight-setup.md) for deferred intent and OSD limitations.
- Never send raw RC, motor, PID, or other FC configuration commands.

## Acceptance and regression criteria

### Action 2

- Complete initial pairing successfully.
- Restart each device 10 times without clearing pairing or requiring confirmation.
- Recover automatically after returning from out of range.
- Complete 20 consecutive start/stop operations after reconnection.

### Public R SDK

- Complete Action 4 pairing and reconnection regression.
- Complete 20 BOOT recording toggles without lockup.
- Match LCD mode, settings, battery, and duration to the camera.

### Betaflight

- Verify the API version gate.
- Perform 20 USER1 activation/deactivation cycles with one command per transition; change AUX assignments in BF without changing ESP32 firmware.
- Update both OSD lines within the 16-byte limit; never present unknown/disconnected values as current data.
- Verify module power loss, restart, and BLE loss do not affect the FC flight-control path.

General UART/OSD hardware confirmation does not establish completion of every repetition and fault-injection criterion above. See [verification status](verification-status.md).
