# Building and flashing

## Toolchain

The verified toolchain is ESP-IDF v5.5.5. Use the terminal supplied by the ESP-IDF installer or run your SDK's export script so that `idf.py`, Python, CMake, Ninja, and the RISC-V compiler are available. No developer-specific installation path is required.

## Board builds and first USB installation

Use fresh 1.1 build directories; old generated configurations use an incompatible single-app layout. The build rejects missing rollback, a wrong partition CSV or nonstandard board UART/Flash settings.

```powershell
.\tools\passthrough.ps1 -Board c3
.\tools\passthrough.ps1 -Board c6
.\tools\passthrough.ps1 -Board c3 -IdfArguments @('-p','COMxx','flash','monitor')
```

Change `c3` to `c6` for the LCD board. The existing `c3-zero.ps1` wrapper also uses the new C3 build directory. In an ESP-IDF shell:

```sh
idf.py -B build-passthrough-c3 -DIDF_TARGET=esp32c3 -DSDKCONFIG=build-passthrough-c3/sdkconfig -DSDKCONFIG_DEFAULTS=config/sdkconfig.c3-zero.defaults build
idf.py -B build-passthrough-c6 -DIDF_TARGET=esp32c6 -DSDKCONFIG=build-passthrough-c6/sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build
```

Component Manager downloads LVGL on the first C6 build. C3 excludes LCD dependencies and disables Component Manager.

## Output and version

`version.txt` supplies version 1.1.0. Application files are `rotorrec_esp32c3.bin` and `rotorrec_esp32c6.bin`; project identity is embedded in the app descriptor so the update receiver can reject the wrong board. Each contains all camera adapters; selection is stored in NVS.

Use `idf.py flash` (or the wrapper above) for the first USB install: it writes the bootloader, partition table, initial OTA metadata and app. An app-only update cannot migrate a 1.0 installation. Avoid `erase-flash` if retaining existing pairing profiles. Existing NVS remains at 0x9000 with size 0x6000.

For subsequent updates, generate a validated package:

```sh
python tools/rotorrec_manager.py pack build-passthrough-c3/rotorrec_esp32c3.bin build-passthrough-c3/rotorrec-c3-update.zip
python tools/rotorrec_manager.py pack build-passthrough-c6/rotorrec_esp32c6.bin build-passthrough-c6/rotorrec-c6-update.zip
```

The package contains the application and its manifest, never a bootloader or partition table. See [passthrough setup](passthrough.md).

## Flashing and monitoring

Replace `PORT` or `COMxx` with the actual serial port. Exit the monitor with Ctrl+]. If automatic download mode fails, hold BOOT, press and release RST, then release BOOT.

## LCD board requirements

- Physical panel: 172x320; application display: 320x172 landscape.
- LVGL rotation: `LV_DISP_ROT_270`; apply the 34-pixel offset on the Y axis after rotation.
- Draw buffer: 320x20 pixels; initialize SPI2 and use a 40 MHz LCD clock.

## Host tests

Windows requires Visual Studio C++ Build Tools and the Windows SDK:

```powershell
.\tools\test-betaflight.ps1
.\tools\test-gopro.ps1
.\tools\test-management.ps1
```

Host tests and build commands do not flash hardware. See [verification status](verification-status.md) for hardware results.
