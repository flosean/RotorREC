# Betaflight UART control and OSD

**Hardware status:** The project owner has tested Betaflight UART communication and OSD functionality on real hardware and confirmed that both work correctly. Confirmation recorded on 2026-09-18.

## Operation

Use **USER1** in Betaflight Modes as the recording switch: active starts recording; inactive stops it. AUX channel, direction, and range are configured in Betaflight. RotorREC does not bind a fixed RC channel or ARM state.

After startup, UART loss, RXLOSS, or FAILSAFE recovery, hold USER1 inactive for at least 200 ms before activating control. This initial inactive position does not stop an existing recording. During normal operation, each stable mode change produces one explicit start or stop request; a failed request does not cause repeated toggling.

If the camera is not ready, the current switch intent waits for readiness and valid status. A newer switch position replaces it. Invalid FC data clears the intent and requires a return to the inactive position. Camera buttons and BOOT remain available; without a new mode transition, RotorREC does not continuously force the camera to match the switch.

## Default wiring

| Board | ESP32 TX to FC RX | ESP32 RX to FC TX |
| --- | --- | --- |
| ESP32-C6-LCD-1.47 | GPIO18 | GPIO19 |
| ESP32-C3-Zero | GP0 | GP1 |

Connect GND to GND. C3 GPIO18/19 are USB pins and must not use the C6 wiring. See [C3 wiring](esp32-c3-zero.md).

- Use 3.3 V logic, 115200 baud, 8N1, no inversion, and no flow control. USB remains available for power and logs.
- For initial bench tests, power the ESP32 over USB. Do not connect battery voltage to GPIO or combine supplies without checking the power circuit.
- Select an unused FC UART and enable standard bidirectional MSP at the same baud rate. Do not share a receiver, GPS, or video transmitter UART.
- RotorREC writes Custom Messages to the FC; the existing video transmitter/goggles OSD path stays in place. The ESP32 is not a DisplayPort video device.
- In `menuconfig` under `RotorREC serial bridge`, configure UART enablement, pins, baud rate, and the initial USER1-USER4 default (permanent IDs 40-43). The 1.2 manager can save a USER override without rebuilding. Board checks reject reserved LCD, USB, BOOT, and other protected pins.

## Flight controller setup

1. Enable MSP at 115200 baud on the selected UART. RotorREC checks `BTFL` and MSP API major 1, minor >=47 before controlling the camera. The target is Betaflight 2025.12 or newer.
2. Assign **USER1** to the desired AUX range in Modes.
3. If USER1 is missing, check PINIO/USER support and existing `pinio_box` assignments. Betaflight exposes USER modes monitored by PinioBox. Do not overwrite all assignments or repurpose existing PINIO resources; some boards use them for video transmitter power. Select an unused USER mode and match its ID in RotorREC if needed.
4. Enable and position **Custom Message 1** and **Custom Message 2** in the OSD layout. Optional four-line mode also requires 3/4. Fresh two-line setups leave 3/4 untouched; after four-line use, RotorREC continues clearing its previously owned 3/4 in two-line mode. See [camera settings](camera-settings.md).

## OSD display

| Element | Examples |
| --- | --- |
| Custom Message 1 | `CAM REC`, `CAM STANDBY`, `CAM BUSY`, `CAM STATUS ?`, `CAM NO LINK` |
| Custom Message 2 | `CAM BAT 53%`, or `CAM BAT --` when no valid value is available |

Recording status comes from the camera, not from switch position or command acknowledgement. Action 2 battery data represents the verified camera percentage; separate camera/display-module battery values are not claimed.

Text updates when changed and is resent on reconnection. A five-second RAM refresh restores text after an FC restart too fast to be detected by polling. RotorREC never writes EEPROM. OSD writes require acknowledgements; failed writes remain eligible for retry, while rejection suspends OSD writes until a new MSP session.

**If the ESP32 loses power or the UART cable disconnects, it cannot clear the FC's last text. Custom Messages may remain visible because there is no FC-side TTL. A static `CAM REC` is not proof that the module is still connected.** BLE loss with working UART can update the text to `CAM NO LINK`.

## Implementation and verification

- Dedicated UART1 task with native MSPv2, CRC, fragmented receive handling, a 256-byte payload limit, noise recovery, and a 150 ms reply timeout. Status polling is approximately 10 Hz, slowed on errors; only one request is outstanding at a time.
- `MSP_BOXIDS` refreshes every five seconds and maps permanent mode IDs to `MSP_STATUS` bits. Extended flags support more than 32 modes; bit 40 is not hard-coded. Missing or malformed mappings prevent control.
- FAILSAFE mode and FAILSAFE, RX_FAILSAFE, and BOXFAILSAFE arming-disable flags block recording changes. Invalid replies and UART timeouts do not mean the switch is inactive.
- Requests are limited to API_VERSION, FC_VARIANT, BOXIDS, STATUS, and SET_TEXT. No RC injection, ARM, motor, PID, FC settings, or EEPROM writes are sent.
- Camera commands run outside the UART polling task and share gating with BOOT. Action 2 waits for a fresh 02/70 state after acknowledgement; unverified 02/80 fields cannot overwrite confirmed state.
- Host tests cover framing, noise, CRC, length, mode ordering, extended bits, failsafe flags, 20 switch cycles, duplicate prevention, readiness, busy state, timer rollover, unknown battery, and OSD text.
- UART and OSD operation are hardware verified by the project owner. No specific board/FC firmware combination, cycle count, or complete fault-injection matrix was supplied with that confirmation; repeated stress and fault tests remain separately tracked.

## Repeatable bench checks

Remove propellers before testing.

1. Confirm UART/API and USER mode detection. Compare OSD state and battery with the camera.
2. Start inactive, then perform 20 active/inactive cycles. Confirm one recording command per transition and matching OSD updates.
3. Use the camera's own record button and confirm the OSD follows actual status.
4. During recording, test receiver, UART, and BLE loss. Confirm missing control data does not send stop. After recovery, return the switch inactive before trying a new command.
5. Restart the module and FC independently. Check startup behavior with an active switch and observe the stale-text limitation.

## Reference sources

- [Modes and permanent IDs](https://betaflight.com/docs/wiki/guides/current/Modes) and [PinioBox / USER visibility](https://betaflight.com/docs/wiki/guides/current/Pinio-and-PinioBox).
- [MSP_BOXIDS and packed mode flags](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp_box.c).
- [MSP_STATUS and SET_TEXT handling](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp.c) and [MSPv2 text constants](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp_protocol_v2_betaflight.h).
- [Failsafe flags](https://github.com/betaflight/betaflight/blob/master/src/main/fc/runtime_config.h) and [RXLOSS handling](https://github.com/betaflight/betaflight/blob/master/src/main/flight/failsafe.c).
- [Waveshare C6 resources](https://docs.waveshare.com/ESP32-C6-LCD-1.47/Resources-And-Documents).

These references were reviewed on 2026-09-12. Upstream master is not necessarily the firmware running on a particular FC.
