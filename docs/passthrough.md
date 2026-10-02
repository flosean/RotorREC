# Betaflight passthrough: camera selection and firmware updates

RotorREC 1.1 includes a local GUI and CLI. Connect the computer to the **flight controller USB**, select its port, and use the existing RotorREC UART wiring. The manager opens Betaflight serial passthrough; RotorREC receives settings and firmware directly on UART1. It does not enter the ESP ROM downloader or require BOOT/EN wiring.

The 1.2 development firmware adds saved USER/OSD/LED/battery settings and Link Pause through the same manager. See [camera settings](camera-settings.md) for commands, restart verification and acceptance. No additional USB migration is needed when the 1.1 dual-slot layout is already installed.

**Status: both board builds and automated tests pass. Real FC passthrough, radio/camera switching, GUI interaction and physical power-cut/rollback tests are still pending. Previous UART/OSD hardware results do not validate this new update path.**

## First installation / migration from 1.0

Every existing single-app installation needs **one USB-C installation on the ESP board**. Build and flash the complete 1.1 layout:

```powershell
.\tools\passthrough.ps1 -Board c3
.\tools\passthrough.ps1 -Board c3 -IdfArguments @('-p','COMxx','flash')
```

Use `-Board c6` for the Waveshare C6-LCD-1.47. This installs bootloader, partition table, initial OTA metadata and application. It preserves the NVS region; do not erase all Flash unless intentionally clearing profiles. An app-only `.bin` cannot migrate 1.0. Keep ESP USB accessible for recovery if no application can run.

Fixed supported wiring (3.3V UART, 115200, 8N1):

| Board | ESP TX → FC RX | ESP RX ← FC TX | Flash |
| --- | --- | --- | --- |
| C3-Zero | GP0 | GP1 | 4 MB |
| C6-LCD-1.47 | GPIO18 | GPIO19 | 8 MB |

Use common ground and an FC UART with MSP enabled at 115200. Some FCs do not supply this peripheral from USB; provide its normal regulated supply when needed. Bench operation only, with propellers removed.

## Open the manager

Requires Python 3.10+ and pyserial 3.5. The GUI also requires tkinter (included in standard Windows Python).

```sh
python -m pip install pyserial==3.5
python tools/rotorrec_manager.py gui
```

1. Close Betaflight Configurator / serial monitors.
2. Select the FC port and the UART wired to RotorREC. A blank UART field auto-selects only when exactly one hardware UART is configured for MSP. The tool never probes every attached peripheral.
3. Use **Read status**, **Save camera & restart**, **Pair camera**, or **Update firmware**.
4. After the first connection, the GUI checks “already in passthrough” automatically for subsequent operations. Uncheck it after power-cycling the FC.
5. When finished, power-cycle the **FC** to leave passthrough and restore MSP/OSD/USER control. Restarting only the ESP cannot exit FC passthrough.

If an operation loses its connection, an abandoned session expires in 30 seconds. Reconnect using the already-in-passthrough option, or power-cycle the FC for a fresh connection. A GUI window cannot close during an operation; **Cancel update** stops before committing the image. An in-flight packet may finish first.

## Camera selection

| Selection | Meaning |
| --- | --- |
| `action2` | DJI Action 2 legacy DUML; default |
| `dji-rsdk` | DJI public R SDK / existing Action 4 path |
| `gopro` | Official Open GoPro BLE; hardware validation pending |

Saving writes one NVS setting and restarts the ESP. Only the selected adapter initializes BLE. Each adapter has its own saved target; changing brands does not erase other profiles. The first version selects the protocol, not a device from a multi-camera scan list. Keep other cameras out of pairing mode and confirm pairing on the camera if requested. GoPro must be in Video mode.

Management pauses automatic USER recording commands and button actions. It does not send STOP on connection or disconnection. Camera changes, reboot and update are rejected while recording, saving or a camera command is pending. A camera can still be operated physically; keep it idle during maintenance. Disconnected/offline cameras do not prevent firmware recovery.

CLI equivalents (global options precede the command):

```sh
python tools/rotorrec_manager.py --port COM7 --uart 2 info
python tools/rotorrec_manager.py --port COM7 --direct camera action2
python tools/rotorrec_manager.py --port COM7 --direct pair
```

`--direct` means the FC is already forwarding this UART; it does **not** mean connect to the ESP USB console. Pair polls actual status for up to 60 seconds and reports unconfirmed pairing as a failure, not success.

## Update packages

Build the matching board image and create its package:

```sh
python tools/rotorrec_manager.py pack build-passthrough-c3/rotorrec_esp32c3.bin build-passthrough-c3/rotorrec-c3-update.zip
python tools/rotorrec_manager.py --port COM7 --uart 2 update build-passthrough-c3/rotorrec-c3-update.zip
```

For C6 use `build-passthrough-c6/rotorrec_esp32c6.bin`. Only matching RotorREC app images are accepted. A ZIP contains exactly `manifest.json` and `app.bin`; the manifest includes board, protocol/layout versions, file size, SHA-256 and ELF fingerprint. These checks detect corruption/mismatch; they are not a publisher signature. Use trusted local build/release files.

The inactive slot receives 1024-byte chunks with incremental erase and stop-and-wait ACKs. The receiver rejects incorrect offsets, oversized/mismatched images and damaged frames. An exact retransmission returns the previous response without repeating a write, pairing or settings commit. After full SHA-256 and ESP-IDF image validation, it switches the boot slot. The tool then restarts the ESP and checks both version **and ELF fingerprint**, including same-version updates, before reporting completion.

An interrupted transfer leaves the selected boot image unchanged. Restart the transfer from the beginning; there is no partial resume. If transfer completion was committed but its reply was lost, the device restarts into the validated image after the session timeout. Inspect status before assuming the update failed or succeeded.

Two app slots and IDF rollback are enabled. On a new image's first startup, a 20-second restart timer guards initialization; after two seconds of main-loop operation, working NVS/UART service permits confirmation. Camera availability is not a health requirement. A crash/reset before confirmation returns to the previous working app. This does not guarantee recovery from every later application bug or bootloader/partition-table corruption. The initial USB-installed app has no previous valid image until a successful subsequent update exists.

At 115200 8N1, 1 MiB takes at least about 91 seconds before protocol/Flash overhead. The GUI reports actual transfer progress; do not interpret 100% bytes sent as verified boot completion.

## Developer protocol and tests

Wire layout, little endian: `RREC` magic (4), version `1` (1), command (1), nonzero session (4), sequence (4), payload length (2), payload (0–1040), CRC32 (4) over header and payload. Responses set command bit 7 and start their payload with a 32-bit ESP error code; INFO/HELLO append JSON. HELLO starts at sequence 0. Subsequent requests increment by one; one request is in flight. Different active sessions and stale/reordered requests are ignored. A partial frame expires after 300ms between bytes; the session expires after 30 seconds without an accepted request.

BEGIN is `<size:u32, layout:u32, board:char[32], version:char[32], sha256:byte[32]>`; DATA is `<offset:u32, bytes>`, maximum 1024 data bytes. Strings are NUL-padded. Camera IDs are 1=R SDK, 2=Action 2, 3=GoPro. The first DATA chunk must contain the full image/app descriptor (at least 288 bytes). Bootloader and partition-table changes require USB installation and a new layout version.

```powershell
.\tools\test-management.ps1
.\tools\test-betaflight.ps1
.\tools\test-gopro.ps1
```

Management tests compile the **actual receiver/state machine** against Windows NVS/OTA fault-injection stubs; native SHA-256 checks use Windows BCrypt. They cover corrupted/random framing, exact replays, settings persistence/failure, concurrent-client rejection, incorrect offset/board/size, hash/write/image-validation/boot-selection failures, cancellation and session expiry. Python tests exercise actual host framing, packages, lost-ACK retransmission, update/reboot verification, cancellation and UART selection. The stubs do not simulate physical Flash/bootloader behavior or BLE.

Before release, record FC model/BF version and camera model/firmware, then complete on **both boards**: initial USB migration preserving profiles; passthrough identification; all camera switches and pairing; USER/OSD regression after FC power-cycle; update twice (both slots); unplug during transfer; cut power during boot selection/first boot; deliberately crashing pending image rollback; GUI reconnect/cancel; actual free heap and 20 recording cycles. No hardware has been flashed by this implementation task.
