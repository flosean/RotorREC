# RotorREC

**ESP32-based action camera control for Betaflight, with recording status and battery feedback on OSD.**

RotorREC 是供 FPV 使用的運動相機控制器：ESP32 透過 BLE 連接相機，透過 UART/MSPv2 讀取 Betaflight USER 模式，控制開始／停止錄影，並將相機實際狀態與電量回傳至 OSD。

目前版本：**1.0.0**。這是首次公開原始碼版本；飛控 UART／OSD 的端到端實機驗收仍待完成，版本號不代表所有硬體情境已驗證。

## 功能

- DJI Action 2 舊版 BLE/DUML 協議：配對、保存目標、自動重連、錄影控制、實際錄影狀態與電量。
- DJI 公開 R SDK 路徑：Action 4 的連線、錄影控制與規格讀取曾通過實機驗證。
- Betaflight USER1～USER4 模式控制；預設 USER1，AUX 通道在 Betaflight Modes 自由指派。
- OSD Custom Message 1 顯示相機狀態，Custom Message 2 顯示相機電量。
- BOOT 短按切換錄影，長按 1.2 秒重新搜尋／配對。
- ESP32-C3-Zero RGB 指示燈，或 ESP32-C6-LCD-1.47 螢幕介面。

## 支援狀態

| 項目 | 1.0.0 狀態 |
| --- | --- |
| DJI Action 2 | 已實測配對、錄影與狀態回報；重啟／超距離恢復有單輪驗證，完整壓力回歸待完成 |
| DJI Action 4 | 既有版本通過連線、錄影與規格讀取；目前模組化版本待硬體回歸 |
| ESP32-C3-Zero | 已燒錄並取得 Action 2 狀態／電量；目前 LED 顏色與完整操作驗收待完成 |
| ESP32-C6-LCD-1.47 | LCD 曾通過實機驗證；目前版本待硬體回歸 |
| Betaflight UART／OSD | 已實作並有主機測試；端到端飛控實測待完成 |
| GoPro | 規劃中，尚未實作 |

詳細限制見 [驗證狀態](docs/verification-status.md)。

## 硬體與接線

| 開發板 | ESP32 TX → FC RX | ESP32 RX ← FC TX | 介面 |
| --- | --- | --- | --- |
| Waveshare ESP32-C3-Zero（4 MB） | GP0 | GP1 | BOOT、GPIO10 RGB、USB 日誌 |
| Waveshare ESP32-C6-LCD-1.47（8 MB） | GPIO18 | GPIO19 | BOOT、LCD、USB 日誌 |

兩端共地，UART 為 **3.3 V 邏輯、115200 baud、8N1**。首次測試使用 USB 供電；不要把電池電壓或 5 V 接到 GPIO。

Betaflight 需要 **2025.12 或更新版本、MSP API 1.47 以上**。在空閒 UART 啟用 MSP，將 AUX 開關指派到 USER1，並啟用兩行 OSD Custom Message。首次測試請拆槳，開關先停用再啟用。完整步驟與失聯行為見 [Betaflight 設定](docs/betaflight-setup.md)。

## 快速建置

使用 **ESP-IDF v5.5.5**，先開啟已初始化的 ESP-IDF 終端機，再取得專案：

```sh
git clone https://github.com/flosean/RotorREC.git
cd RotorREC
```

ESP32-C3-Zero（無螢幕、無下載元件需求）：

```sh
idf.py -B build-c3-zero -DIDF_TARGET=esp32c3 -DSDKCONFIG=build-c3-zero/sdkconfig -DSDKCONFIG_DEFAULTS=config/sdkconfig.c3-zero.defaults build
idf.py -B build-c3-zero -p PORT flash monitor
```

ESP32-C6-LCD-1.47（首次建置會下載 LVGL）：

```sh
idf.py -B build-modular -DIDF_TARGET=esp32c6 build
idf.py -B build-modular -p PORT flash monitor
```

將 `PORT` 換成實際序列埠。兩種板型必須使用不同建置目錄，不能互刷韌體。Windows PowerShell wrapper、工具需求與操作細節見 [建置與燒錄](docs/build-and-flash.md)。

## 程式架構

```text
main/
  app_main.c       啟動與主迴圈
  camera/          共用相機狀態與控制；dji/ 放兩種獨立協議
  betaflight/      MSPv2、USER 模式政策、UART 與 OSD
  platform/        按鍵、LCD、RGB 硬體封裝
  ui/              LCD 畫面、無螢幕日誌與指示燈狀態
components/        必要的 DJI 與 Waveshare 元件
config/            C3 板型預設配置
tests/             主機端協議、控制政策與燈號測試
tools/             建置與測試入口
docs/              設定、架構、需求與驗證說明
version.txt        韌體版本來源
```

相機協議與 Betaflight 透過共用相機控制介面整合，UI 不直接存取 BLE。詳見 [架構](docs/architecture.md)。Git 僅追蹤可建置的現行程式與文件；本機舊版快照、封包、研究副本、PCB 草稿、個人設定與建置產物皆排除。

## 測試

Windows 安裝 Visual Studio C++ Build Tools 後：

```powershell
.\tools\test-betaflight.ps1
```

測試涵蓋 MSP frame/parser、USER 控制政策、OSD 文字與 LED 狀態／顏色序列化。主機測試無法取代 BLE、UART 和飛控實機驗收。

## 文件與授權

- [文件索引](docs/README.md)
- [版本紀錄](CHANGELOG.md)
- [第三方來源與授權](THIRD_PARTY_NOTICES.md)

本版本尚未為專案原創部分指定通用授權；第三方元件保留各自授權與聲明。RotorREC 為獨立專案。
