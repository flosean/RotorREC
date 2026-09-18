# ESP32-C3-Zero

The headless RotorREC board uses the Waveshare ESP32-C3-Zero (C3FH4, 4 MB flash). It shares camera and Betaflight logic with C6 and does not compile LCD or LVGL dependencies.

## Controls

- Short-press BOOT (GPIO9): toggle recording when ready.
- Hold BOOT for 1.2 seconds: scan and pair again.
- RESET: restart the module.
- USB logs: connection status, battery level, and R SDK pairing code.
- GPIO10 RGB: indication based on actual camera status.

| Indicator | Meaning |
| --- | --- |
| Steady green | Connected with confirmed standby status |
| Slow green blink | Camera reports recording |
| Slow red blink | Disconnected, scan failed, or pairing rejected |
| Fast red blink | Scanning, pairing, or reconnecting |
| Fast yellow blink | Waiting for valid status, command confirmation, or camera storage |

Slow blinking is 1 second on and 1 second off; fast blinking is 0.2 seconds on and 0.2 seconds off. This board uses RGB byte order. Visual verification of the color correction remains open; other WS2812 boards may use a different order.

## Wiring

| C3-Zero | Flight controller |
| --- | --- |
| GP0 (UART1 TX) | RX on an unused MSP UART |
| GP1 (UART1 RX) | TX on the same MSP UART |
| GND | GND |

Use 115200 baud, 8N1, non-inverted 3.3 V logic. GP0 and GP1 are GPIO identifiers, not physical pin numbers. UART0 GP21/GP20 is not used by default to keep boot messages out of MSP traffic. Do not reuse C6 GPIO18/19 wiring: these pins provide USB on C3.

Initially power the board over USB. Never connect battery voltage or 5 V to GPIO pins. GPIO2/8/9 are boot-related, GPIO10 drives the LED, and GPIO12-17 serve flash; the firmware rejects these pins for the Betaflight UART.

## Build and flash

Open an ESP-IDF PowerShell terminal:

```powershell
.\tools\c3-zero.ps1 -IdfArguments @('build')
.\tools\c3-zero.ps1 -IdfArguments @('menuconfig')
.\tools\c3-zero.ps1 -IdfArguments @('-p','COMxx','flash','monitor')
```

Replace COMxx with the actual port. The wrapper uses `build-c3-zero/`, its own sdkconfig, and `config/sdkconfig.c3-zero.defaults`. It does not change C6 settings. The application image is `build-c3-zero/rotorrec.bin`; the flash command also writes the bootloader and partition table.

Other systems can use the idf.py commands in [building and flashing](build-and-flash.md). Do not flash C6 images onto C3. If automatic download fails, hold BOOT, press RESET, and release BOOT. The serial port may change in download mode. Exit monitoring with Ctrl+].

## First pairing and checks

1. Connect USB and confirm the log identifies the C3 board and UART TX0/RX1.
2. Turn off other camera remotes and turn on the camera near the module.
3. Initial discovery tries the public R SDK before the Action 2 path. Accept any confirmation shown on the camera. Hold BOOT to retry a failed scan.
4. Check the Bluetooth icon, actual recording control, and battery level. Then test independent restarts and out-of-range recovery.
5. Remove propellers before connecting the flight controller and follow [Betaflight setup](betaflight-setup.md).

Action 2 standby and battery reception have been verified on C3. Betaflight UART and OSD have also been confirmed working on real hardware by the project owner. See [verification status](verification-status.md) for remaining operational and RGB checks.

Hardware reference: [Waveshare C3-Zero documentation](https://docs.waveshare.com/ESP32-C3-Zero).
