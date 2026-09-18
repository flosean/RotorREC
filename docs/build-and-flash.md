# Building and flashing

## Toolchain

The verified toolchain is ESP-IDF v5.5.5. Use the terminal supplied by the ESP-IDF installer or run your SDK's export script so that `idf.py`, Python, CMake, Ninja, and the RISC-V compiler are available. No developer-specific installation path is required.

## ESP32-C3-Zero

```sh
idf.py -B build-c3-zero -DIDF_TARGET=esp32c3 -DSDKCONFIG=build-c3-zero/sdkconfig -DSDKCONFIG_DEFAULTS=config/sdkconfig.c3-zero.defaults build
idf.py -B build-c3-zero -p PORT flash monitor
```

The PowerShell wrapper selects the isolated C3 configuration and build directory:

```powershell
.\tools\c3-zero.ps1 -IdfArguments @('build')
.\tools\c3-zero.ps1 -IdfArguments @('-p','COMxx','flash','monitor')
```

## ESP32-C6-LCD-1.47

```sh
idf.py -B build-modular -DIDF_TARGET=esp32c6 build
idf.py -B build-modular -p PORT flash monitor
```

PowerShell:

```powershell
.\tools\idf.ps1 -IdfArguments @('-B','build-modular','-DIDF_TARGET=esp32c6','build')
.\tools\idf.ps1 -IdfArguments @('-B','build-modular','-p','COMxx','flash','monitor')
```

Component Manager downloads LVGL on the first C6 build using `dependencies.lock`. C3 does not use LVGL and disables Component Manager.

## Output and version

Each build directory contains `rotorrec.bin`, the application image. Use `idf.py flash` to write the correct bootloader and partition table as well; the application image is not a whole-flash image.

`version.txt` is the single version source. CMake reads it into ESP-IDF `PROJECT_VER`, and the startup log prints the RotorREC version. Version 1.0.0 renamed the output from `bf_cam.bin` to `rotorrec.bin`. Internal `CONFIG_BF_CAM_*` names remain for configuration compatibility.

Do not run C3 set-target commands against the existing C6 build directory. Generated sdkconfig files and build output are excluded from Git; new environments generate settings from board defaults.

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
```

Host tests and build commands do not flash hardware. See [verification status](verification-status.md) for hardware results.
