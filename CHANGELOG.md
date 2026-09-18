# Changelog

## 1.0.0 — 2026-09-18

首次以 RotorREC 名稱發布現行原始碼。

- ESP32-C3-Zero 與 ESP32-C6-LCD-1.47 分開建置，共用相機與 Betaflight 邏輯。
- DJI Action 2 legacy BLE/DUML 及 DJI 公開 R SDK 協議路徑。
- 配對、保存目標、重連、錄影控制與相機狀態回報。
- Betaflight MSPv2、USER1～USER4 控制、兩行 OSD 與失聯控制政策。
- LCD、BOOT 按鍵、C3 RGB 指示燈與 USB 狀態日誌。
- 主機測試、板型預設配置、使用與架構文件。
- `version.txt` 定義韌體版本 1.0.0，輸出名稱改為 `rotorrec.bin`。
- 排除建置快取、封包、歷史快照、下載參考資料與未完成 PCB 草稿。

已知限制：Betaflight UART／OSD 尚待端到端實機驗收；Action 4 模組化回歸、重連壓力測試及 C3 LED 肉眼驗收尚未完成；GoPro 尚未實作。完整說明見 [驗證狀態](docs/verification-status.md)。
