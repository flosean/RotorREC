# Architecture

## Design principles

The application uses one shared camera control interface and separate adapters for genuinely different protocols. DJI currently requires two adapters. GoPro has no implementation, so there is no placeholder driver or speculative generic framework.

```text
app_main
  platform/display -> Waveshare LCD component -> LVGL
  platform/boot_button
  ui/dashboard -> camera_controller_state
  camera/camera_controller
    camera/dji/dji_rs_sdk -> DJI public R SDK component
    camera/dji/dji_action2 -> legacy DUML, NVS, keepalive
  betaflight
    msp_v2 -> framing and CRC8-DVB-S2
    osd_messages -> MSP2_SET_TEXT / Custom Message
    bf_control -> mode mapping, switch policy, actual camera OSD
    bf_bridge -> UART1 request/reply, polling, ACK cache
```

## Module responsibilities

### Board builds

- C6-LCD-1.47 includes LCD, LVGL, and the dashboard, using `build-modular` and the root sdkconfig.
- C3-Zero uses `tools/c3-zero.ps1`, `build-c3-zero`, and an independent sdkconfig. It includes `ui/headless_status` and excludes display dependencies. Component Manager is disabled to preserve the C6 dependency lock.
- Camera and Betaflight modules are shared. UART defaults and protected pins depend on the board; C3 must not reuse C6 USB/flash pin assumptions.
- `ui/status_indicator` maps state to testable blink patterns. `platform/status_led` drives GPIO10 through RMT. `status_led_color.h` provides testable serialization, using RGB for this board. RMT failure disables LED output without stopping the camera workflow.

### `main/camera/camera_controller.*`

- Provides pairing, recording-button, explicit recording-request, and state-query interfaces.
- Tries the public R SDK before falling back to the Action 2 legacy path.
- Reconnects directly when an Action 2 profile is already saved.
- Keeps DJI SDK calls and BLE globals out of UI code.

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

### `components/dji_camera_sdk`

A local copy of the DJI Osmo controller demo subset. `ble.c/.h` and `data.c` include Action 2 diagnostics, address type handling, and targeted reconnection. Third-party notices remain in the component directory.

### `components/waveshare_c6_lcd`

The board's ST7789 and LVGL port, including the rotated 34-pixel offset, 320x20 draw buffer, SPI2 initialization, and 40 MHz clock.

## Dependency rules

- UI depends on shared camera types, not `ble.h` or DJI protocol headers.
- Platform code contains no camera protocols.
- Action 2 and R SDK adapters do not include each other.
- Betaflight does not depend on DJI; a future GoPro adapter should share its status output.
- Third-party code stays in `components`; application behavior stays in `main`.

## Adding a camera

Add `main/camera/gopro/` only when implementation begins, then add a real selection branch in the controller. If the shared operations remain connect, toggle recording, and get state, keep the existing interface. Reconsider a function table only when a second non-DJI adapter warrants it.
