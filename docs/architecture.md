# Architecture

## Design principles

The application uses one shared camera control interface and separate protocol adapters. DJI has public R SDK and legacy Action 2 paths. Each board image contains all adapters. NVS selects Action 2 (default), DJI R SDK or GoPro at startup; only that adapter initializes BLE. Switching requires an ESP restart.

```text
app_main
  platform/display -> Waveshare LCD component -> LVGL
  platform/boot_button
  ui/dashboard -> camera_controller_state
  camera/camera_controller
    camera/dji/dji_rs_sdk -> DJI public R SDK component
    camera/dji/dji_action2 -> legacy DUML, NVS, keepalive
    camera/gopro/gopro -> Open GoPro BLE, bonding, NVS, keepalive
    camera/gopro/gopro_protocol -> packet assembly and status parsing
  betaflight
    msp_v2 -> framing and CRC8-DVB-S2
    osd_messages -> MSP2_SET_TEXT / Custom Message
    bf_control -> mode mapping, switch policy, actual camera OSD
    bf_bridge -> UART1 request/reply, polling, ACK cache
```

## Module responsibilities

### Board builds

- C6-LCD-1.47 includes LCD, LVGL, and the dashboard, using `build-passthrough-c6` and an independent sdkconfig.
- C3-Zero uses `tools/c3-zero.ps1`, `build-passthrough-c3`, and an independent sdkconfig. It includes `ui/headless_status` and excludes display dependencies. Component Manager is disabled to preserve the C6 dependency lock.
- Camera and Betaflight modules are shared. UART defaults and protected pins depend on the board; C3 must not reuse C6 USB/flash pin assumptions.
- `ui/status_indicator` maps state to testable blink patterns. `platform/status_led` drives GPIO10 through RMT. `status_led_color.h` provides testable serialization, using RGB for this board. RMT failure disables LED output without stopping the camera workflow.

### `main/camera/camera_controller.*`

- Provides pairing, recording-button, explicit recording-request, and state-query interfaces.
- Uses the explicitly selected DJI protocol without probing the other handshake.
- Reconnects directly when an Action 2 profile is already saved.
- Keeps DJI SDK calls and BLE globals out of UI code.
- When GoPro is selected, delegates to the GoPro worker using the same public control/state interface.

### `main/camera/gopro/*`

- Uses ESP-IDF Bluedroid directly; no DJI transport or pairing frames are reused.
- BLE callbacks copy events into a bounded queue. A single worker owns discovery, subscriptions, ordered request/response transactions, polling and reconnection. Queue overflow invalidates the session.
- Uses the official service/characteristic UUIDs, BLE bonding, hardware-readiness query, API-version query, third-party identification and protobuf pairing completion.
- Reassembles bounded response packets per characteristic; validates complete status TLVs before publishing a snapshot under a lock.
- The shutter request is nonblocking to BOOT/Betaflight. It is sent once, with acknowledgement and subsequent actual-state confirmation. A timeout reconnects instead of blindly retrying.
- Queries encoding, busy, readiness, battery percentage and preset group. Video control requires the video preset group; stale state cannot authorize a command.
- Saves the selected camera in a separate NVS namespace. Long BOOT hold clears that profile and its bond before discovery. Normal reconnection retains bonding and restores notifications.

### `main/camera/dji/dji_rs_sdk.*`

- Wraps public R SDK pairing, status subscription, and recording control.
- Converts DJI enums and frames to the shared `camera_snapshot_t`.
- Does not process Action 2 legacy frames.

### `main/camera/dji/dji_action2.*`

- Owns legacy pairing, session wake, keepalive, recording control, and acknowledgement frames.
- Saves and restores the NVS profile.
- Handles notifications and recording command confirmation.
- Uses verified 02/70 recording state and 0d/02 battery data. Unverified 02/80 fields remain diagnostic and cannot overwrite confirmed state.

### `main/betaflight/*`

- `msp_v2`: framing, stream parsing, length checks, and CRC.
- `osd_messages`: four Custom Message payloads and the 16-byte limit.
- `bf_control`: testable USER mode mapping, debounce and loss-of-link policy, and two camera OSD lines.
- `bf_bridge`: a dedicated UART1 task that validates the FC/API, polls BOXIDS/STATUS, writes volatile OSD text, and handles acknowledgements and timeouts. It depends on the shared controller, not DJI details.
- Non-blocking `request_recording` specifies the desired state and shares command gating with BOOT to avoid blind toggling. Betaflight UART and OSD operation have been confirmed on hardware by the project owner.

### `main/management/*`

- `protocol.c` is a portable, bounded CRC32 framing parser. One session and one request in flight; exact request replays return the cached response without repeating side effects.
- `management.c` owns NVS selection and ESP-IDF OTA transactions. It accepts only a compatible app image, writes the inactive slot, verifies SHA-256 and IDF image validity, then selects it for boot.
- The existing `bf_bridge` task remains the only UART reader/writer. Its polling and disconnected waits service management frames; active management suppresses MSP and USER control. No second reader or UART input flush can consume HELLO.
- Startup has a 20-second restart guard. After two seconds of main-loop operation, NVS/UART health gates pending-image confirmation. Camera presence is not required.
- Management failure never erases all NVS. DJI SDK initialization returns storage errors rather than silently clearing profiles.

### `components/dji_camera_sdk`

A local copy of the DJI Osmo controller demo subset. `ble.c/.h` and `data.c` include Action 2 diagnostics, address type handling, and targeted reconnection. Third-party notices remain in the component directory.

### `components/waveshare_c6_lcd`

The board's ST7789 and LVGL port, including the rotated 34-pixel offset, 320x20 draw buffer, SPI2 initialization, and 40 MHz clock.

## Dependency rules

- UI depends on shared camera types, not `ble.h` or DJI protocol headers.
- Platform code contains no camera protocols.
- Action 2 and R SDK adapters do not include each other.
- Betaflight depends only on shared camera state, used by both DJI and GoPro.
- Third-party code stays in `components`; application behavior stays in `main`.

## Adding a camera

Keep new protocol implementations behind the existing connect, explicit recording request and state-query interface. Current camera-family selection happens at build time; automatic cross-brand discovery and simultaneous camera control are not implemented.
