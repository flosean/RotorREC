# 建置與燒錄

## 環境

驗證工具鏈為 ESP-IDF v5.5.5。使用 ESP-IDF 安裝程式提供的終端機，或執行所安裝 SDK 的 export 腳本，讓 `idf.py`、Python、CMake、Ninja 與 RISC-V 編譯器可用。不需設定任何開發者個人的固定路徑。

## ESP32-C3-Zero

```sh
idf.py -B build-c3-zero -DIDF_TARGET=esp32c3 -DSDKCONFIG=build-c3-zero/sdkconfig -DSDKCONFIG_DEFAULTS=config/sdkconfig.c3-zero.defaults build
idf.py -B build-c3-zero -p PORT flash monitor
```

PowerShell 也可使用下列 wrapper；它固定使用獨立的 C3 配置和目錄：

```powershell
.\tools\c3-zero.ps1 -IdfArguments @('build')
.\tools\c3-zero.ps1 -IdfArguments @('-p','COMxx','flash','monitor')
```

## ESP32-C6-LCD-1.47

```sh
idf.py -B build-modular -DIDF_TARGET=esp32c6 build
idf.py -B build-modular -p PORT flash monitor
```

PowerShell：

```powershell
.\tools\idf.ps1 -IdfArguments @('-B','build-modular','-DIDF_TARGET=esp32c6','build')
.\tools\idf.ps1 -IdfArguments @('-B','build-modular','-p','COMxx','flash','monitor')
```

首次建置由 Component Manager 取得 LVGL，版本由 `dependencies.lock` 固定。C3 不使用 LVGL，並停用 Component Manager。

## 輸出與版本

各建置目錄的 `rotorrec.bin` 為應用程式映像。請使用 `idf.py flash` 一併寫入正確的 bootloader 與 partition table，勿把 app image 當整片映像燒錄。

`version.txt` 是版本的唯一來源；CMake 讀取為 ESP-IDF `PROJECT_VER`，啟動日誌會印出 RotorREC 版本。切換至 1.0.0 後，輸出名稱由舊的 `bf_cam.bin` 改成 `rotorrec.bin`。內部 `CONFIG_BF_CAM_*` 設定名稱保留相容性。

不要對既有 C6 建置目錄執行 C3 的 set-target。`sdkconfig` 和建置產物不進 Git，新環境從板型 defaults 產生設定。

## 燒錄與監看

`PORT`／`COMxx` 必須換成實際埠。退出 monitor 使用 Ctrl+]。無法自動進入下載模式時，按住 BOOT，按下並放開 RST，再放開 BOOT。

## LCD 板級條件

- 實體面板 172×320，應用使用 320×172 landscape。
- LVGL rotation：`LV_DISP_ROT_270`，旋轉後 34-pixel offset 加到 Y 軸。
- draw buffer：320×20 pixels；SPI2 bus 必須初始化，LCD clock 為 40 MHz。

## 主機測試

Windows 需要 Visual Studio C++ Build Tools 與 Windows SDK：

```powershell
.\tools\test-betaflight.ps1
```

主機測試與韌體建置不會燒錄硬體。硬體驗收範圍見 [驗證狀態](verification-status.md)。
