# GoPro setup

The GoPro adapter uses the official Open GoPro BLE interface. It implements scanning, BLE bonding, session setup, saved-target reconnect, keepalive, video recording control and camera-reported status/battery. **No GoPro model or firmware has been verified on hardware in this project yet.**

## Protocol scope

The camera must provide Open GoPro API 2.x, the FEA6 control/query service, GP-0090 management service and the documented one-byte TLV status queries. Consult the [official supported-camera list](https://gopro.github.io/OpenGoPro/) for model and minimum firmware requirements. This does not imply every listed camera has passed RotorREC testing. Legacy undocumented GoPro control and Wi-Fi/HTTP control are outside this implementation.

All supported adapters are now included in each board image. Action 2 remains the default; select `gopro` using the [passthrough manager](passthrough.md). The ESP restarts and initializes only the selected BLE adapter. No GoPro-specific build or menuconfig choice is needed.

## Build

Use the standard board build and initial USB installation described in [building and flashing](build-and-flash.md). Old GoPro-only single-app builds also require this one-time migration before UART updates work.

## Pair and operate

1. Put the camera in its app/Bluetooth pairing mode, and keep other GoPros out of pairing range during the first connection. Initial discovery selects the first matching advertisement.
2. Select GoPro in RotorREC and restart the ESP. It scans, bonds, discovers characteristics and restores response notifications. Session setup checks hardware readiness/API version, identifies a third-party client and finishes pairing through camera management.
3. Select the camera's **Video** preset group. Photo and timelapse groups report unknown recording state to the shared controller and block recording commands. Configure video settings on the camera.
4. Short-press BOOT to start/stop, or use the existing Betaflight USER switch. The USER switch still needs its initial inactive position. OSD reports actual encoding state and battery percentage.
5. Hold BOOT for 1.2 seconds to request fresh pairing. Once the current transaction finishes, the worker disconnects, forgets the saved GoPro address and its BLE bond, then scans again. The DJI profile is separate.

A saved GoPro reconnects automatically after link loss or module restart. Reconnection scans for its stored BLE address; a camera that changes its address or resets pairing may need another BOOT hold and pairing-mode setup. Discovery does not guarantee waking a powered-off camera whose BLE advertisements have stopped.

## State and failure behavior

- Query status 10 (encoding), 8 (busy), 82 (ready), 70 (battery percentage) and 96 (preset group). Encoding is never inferred from a successful shutter acknowledgement.
- Poll normally about once per second, and send keepalive setting 0x5B with value 0x42 about every three seconds. The camera stays awake while the session is maintained.
- A start/stop request is sent once. The adapter waits for the command response and subsequent matching camera status; a missing reply or state confirmation causes reconnection, with no automatic shutter resend.
- A malformed/incomplete status reply cannot partially overwrite state. Data older than five seconds is invalid; disconnection immediately invalidates it when the worker processes the event.
- The adapter uses bounded packet assembly and checks continuation order. Bond keys are stored by Bluedroid; the target address is stored in the `gopro` NVS namespace.
- Recording duration, storage capacity, changing presets and automatic cross-brand selection are not exposed by this adapter.

## Verification

```powershell
.\tools\test-gopro.ps1
.\tools\test-betaflight.ps1
```

Host tests cover packet headers, continuation ordering/wrap, truncation/overflow, independent response streams, status TLV validation, advertisement filtering, and GoPro state through the shared Betaflight policy/OSD. They do not simulate the radio or prove pairing compatibility.

Hardware acceptance must record the camera model/firmware and board, then verify first pairing, module/camera restart, loss-of-range recovery, 20 start/stop cycles, actual recording-file creation, battery against the camera UI, at least 10 minutes of idle keepalive, Photo-mode rejection, camera-busy behavior, and BOOT/USER/OSD operation. Confirm that command timeout never produces a repeated shutter request.

## Official references

- [BLE setup and characteristic UUIDs](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/)
- [Packetization and TLV format](https://gopro.github.io/OpenGoPro/docs/ble/protocol/data_protocol/)
- [Shutter and keepalive](https://gopro.github.io/OpenGoPro/docs/ble/control/)
- [Status IDs](https://gopro.github.io/OpenGoPro/docs/ble/statuses/)
- [Official Python command definitions](https://github.com/gopro/OpenGoPro/blob/main/demos/python/sdk_wireless_camera_control/open_gopro/api/ble_commands.py)
- [Pairing completion protobuf](https://github.com/gopro/OpenGoPro/blob/main/protobuf/network_management.proto)
- [Preset-group IDs (Video = 1000)](https://github.com/gopro/OpenGoPro/blob/main/protobuf/preset_status.proto)

This is an independent implementation from the public specification. GoPro hardware compatibility remains unverified.
