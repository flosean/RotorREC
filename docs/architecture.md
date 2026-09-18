# 程式架構

更新日期：2026-09-12

## 設計原則

目前只有一個共用相機控制介面；真正不同的協議才拆 adapter。DJI 已經有兩個實際 adapter，因此值得建立 seam。GoPro 尚未開發，所以不先建立空白實作或通用框架。

```text
app_main
├─ platform/display ── Waveshare LCD component ── LVGL
├─ platform/boot_button
├─ ui/dashboard ────── camera_controller_state
├─ camera/camera_controller
│  └─ camera/dji
│     ├─ dji_rs_sdk ───── DJI 公開 R SDK component
│     └─ dji_action2 ──── legacy DUML + NVS + keepalive
└─ betaflight
   ├─ msp_v2 ─────────── frame + CRC8-DVB-S2
   ├─ osd_messages ───── MSP2_SET_TEXT / Custom Message
   ├─ bf_control ─────── mode map / switch policy / actual camera OSD
   └─ bf_bridge ──────── UART1 request/reply / polling / ACK cache
```

## 模組責任

### 板型建置

- C6-LCD-1.47：編入 LCD／LVGL／dashboard，保留原 `build-modular` 與根目錄 sdkconfig。
- C3-Zero：`tools/c3-zero.ps1` 使用 `build-c3-zero` 及獨立 sdkconfig；編入 `ui/headless_status`，不編入顯示依賴。只使用本地／IDF 元件，關閉 component manager，避免更動 C6 dependency lock。
- 相機與 Betaflight 模組共用；UART 預設值與保護腳位按板型區分，C3 不可使用 C6 的 USB/Flash 腳位配置。
- C3 的 `ui/status_indicator` 是可主機測試的狀態／閃爍映射；`platform/status_led` 用 GPIO10 硬體 RMT 輸出，`status_led_color.h` 處理可主機測試的資料順序（依此板紅綠對調回報改為 RGB），不介入 BLE 控制。RMT 故障只停用燈輸出，不中止相機主流程。

### `main/camera/camera_controller.*`

- 對應用程式提供唯一的配對、短按錄影與狀態查詢介面。
- 決定先嘗試公開 R SDK，失敗後再轉入 Action 2 legacy 路徑。
- Action 2 已有 profile 時直接進重連流程。
- UI 不直接呼叫 DJI SDK，也不讀 BLE 全域變數。

### `main/camera/dji/dji_rs_sdk.*`

- 包裝 DJI 公開 R SDK 的配對、狀態訂閱與錄影控制。
- 把 DJI SDK 的 enum／frame 轉成共用 `camera_snapshot_t`。
- 此檔不處理 Action 2 legacy frame。

### `main/camera/dji/dji_action2.*`

- 保存 Action 2 legacy 配對、session wake、keepalive、錄影控制與 ACK frame。
- 保存／讀取 NVS profile。
- 處理 notification 與錄影命令確認。
- 02/70 錄影狀態及 0d/02 電量已實測；02/80 未驗證欄位僅診斷，不可覆蓋已確認狀態。

### `main/betaflight/*`

- `msp_v2` 負責 MSPv2 frame、串流 parser、長度檢查與 CRC。
- `osd_messages` 只負責四行 Custom Message payload 與 16-byte 限制。
- `bf_control` 是可在主機測試的 USER 模式映射、去抖／失聯政策及兩行相機 OSD 格式化。
- `bf_bridge` 使用 UART1 獨立任務驗證 FC/API、輪詢 BOXIDS/STATUS、寫入 volatile OSD 文字並處理 ACK／逾時。只依賴共用 camera_controller，不依賴 DJI 細節。
- 相機控制新增非阻塞、明確目標狀態 request_recording；與 BOOT 共用命令門檻，避免盲目 toggle。UART 與 BLE 實機整合仍待測試。

### `components/dji_camera_sdk`

DJI 官方 Osmo controller demo 的本機元件副本。`ble.c/.h` 與 `data.c` 包含本專案為 Action 2 診斷、address type 與指定 MAC 重連所做的修改。第三方授權保留在元件目錄。

### `components/waveshare_c6_lcd`

只保留此開發板所需的 ST7789 與 LVGL port。旋轉後 34-pixel offset、320×20 draw buffer、SPI2 初始化與 40 MHz SPI 都封裝在此元件。

## 相依規則

- `ui` 只能依賴共用 camera types，不可 include `ble.h` 或 DJI protocol header。
- `platform` 不可包含相機協議。
- Action 2 與 R SDK 不互相 include。
- Betaflight 不依賴 DJI；未來 GoPro 也應能共用相同 Betaflight 狀態輸出。
- 第三方碼留在 `components`，專案行為留在 `main`。

## 新增相機的方式

GoPro 真正開始開發時才新增 `main/camera/gopro/`，並讓 `camera_controller` 多一個實際選擇分支。若屆時共同操作仍只有 connect、toggle record、get state，不擴充更大的抽象層；只有出現第二個非 DJI adapter 後，再評估 function table。
