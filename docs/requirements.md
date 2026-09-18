# RotorREC 目前需求

更新日期：2026-09-12

Action 2 本階段範圍：連線／重連、實際錄影狀態與開始停止控制、相機電量。錄影時間與容量研究暫停，不列為交付阻礙。

板型：保留 ESP32-C6-LCD-1.47 螢幕版；新增 ESP32-C3-Zero 無螢幕版（4 MB Flash、BOOT／USB 日誌／BF OSD／RGB 狀態燈）。兩版共用相機與 BF 邏輯，分開 sdkconfig、build 目錄及 UART 預設腳位；C3 使用 GP0 TX／GP1 RX，GPIO10 控制 WS2812。

## 1. 產品目標

使用 ESP32-C6 連接 Betaflight 飛控與運動相機，從飛控的 ARM／AUX 狀態控制錄影，並把相機狀態透過 Betaflight Custom Message 顯示在 OSD。開發板上的 LCD 與 BOOT 按鍵保留作為桌上測試與診斷介面。

## 2. 目前支援範圍

### P0：DJI

需同時保留兩條互不混用的協議路徑：

1. **DJI 公開 R SDK**：供 Action 4 與採用公開協議的新型 DJI 相機使用。
2. **DJI Action 2 legacy DUML**：Action 2 不支援目前的公開 R SDK handshake，必須使用已由實機驗證的舊協議 frame。

不得因兩者同為 DJI 就共用配對 frame 或假設 status 格式相同。

### P1：GoPro

後續只考慮仍有官方公開連接規格的新款 GoPro。GoPro 尚未開始實作，現在只保留需求位置，不建立假的 driver 或未驗證能力。

### 不在目前範圍

- Blackmagic、Sony 與其他相機品牌。
- 同時控制多台 Action 2。
- 手機原生 App。若未來需要設定介面，優先考慮板載 Wi-Fi 網頁。
- 反向控制飛控的馬達、RC、PID 或其他飛行安全參數。

## 3. 配對與重連

### Action 2

- 第一次配對時允許相機顯示確認提示，使用者在相機上接受。
- 配對完成後保存相機 BLE MAC 與 address type 到 NVS。
- 開發板或相機重新開機後，應鎖定已保存的相機並直接恢復 session，不再次要求人工確認。
- 超距離或其他非預期斷線後，依 2026-09-08 使用者要求，約每秒搜尋已配對相機，找到即停止掃描並連線，不作省電退避。連線與服務探索仍保留必要的逾時保護。
- 重新連上後要恢復 legacy notification、session 與 keepalive，然後直接允許錄影開始／停止。
- 長按 BOOT 1.2 秒可中止既有重連並進入全新掃描／配對。
- 相機成功接受配對後，Action 2 畫面應出現 Bluetooth 標示；此項由使用者觀察驗收。
- 現有 legacy pairing request 含由官方遙控器流程取得的固定欄位；它只對目前這台 Action 2 驗證過。後續須確認哪些欄位是 controller ID、序號、配對碼或 CRC，再決定是否參數化。

### DJI 公開 R SDK

- 掃描相機、建立 BLE/GATT 連線並執行公開協議驗證。
- LCD 顯示四位配對碼，使用者在相機端確認。
- 配對後訂閱相機狀態，取得錄影狀態、模式、規格、電量、錄影時間與剩餘容量／時間。
- Action 4 既有能力重構後必須做回歸，不能只以編譯成功視為通過。

## 4. 本機操作介面

- BOOT 短按：已就緒時切換開始／停止錄影。
- BOOT 長按 1.2 秒：重新掃描／配對。
- RST：維持硬體重置功能，不作應用按鍵。
- C3 WS2812：綠燈恆亮表示已連線且待機、綠燈慢閃表示實際錄影、紅燈慢閃表示未連線／搜尋失敗、紅燈快閃表示搜尋／配對／重連；黃燈快閃表示等待狀態／指令確認或儲存中，不以未知資料顯示綠燈。
- LCD 顯示：連線階段、配對碼、協議類型、重連狀態、錄影狀態、錄影規格、電量、錄影時間與診斷資料。
- Action 2 尚未解析完整狀態前，不可自行猜測錄影規格或電量；畫面只顯示已確認的資料。

## 5. Betaflight

- 目標版本：Betaflight 2025.12 或更新版，MSP API 1.47 以上。
- 電氣介面：3.3 V UART，ESP32 UART1 預設 GPIO18 TX、GPIO19 RX、115200 8N1，交叉接 FC 的空閒 MSP UART 並共地；可由 menuconfig 調整。
- 啟動後先查 `MSP_API_VERSION`，版本不足時不啟用自動控制。
- 讀取：`MSP_FC_VARIANT`、`MSP_BOXIDS`、`MSP_STATUS`。AUX 指派與範圍留在 BF Modes；ESP32 不綁原始 RC 通道。
- 寫入 OSD：原生 MSPv2 `MSP2_SET_TEXT (0x3007)`，type 7～10 對應 Custom Message 1～4，每行最多 16 bytes。
- 本期只使用 Custom Message 1 顯示實際錄影／連線狀態、Custom Message 2 顯示相機電量；第 3、4 行不動。文字變更、MSP 重連時更新，另每 5 秒 RAM 補寫以恢復快速 FC 重啟後的文字；完全不發 `MSP_EEPROM_WRITE`。
- 預設自動化改為 USER1（permanent ID 40，可設定 USER1～4）：模式啟用開始、停用停止，不綁 ARM。需先確認模式映射、有效非失效飛控狀態、相機已就緒且狀態有效，200 ms 去抖且防重送。失聯不發停止指令；啟動／FC 或接收機恢復後先見停用位置才接受新指令。細節與限制見 [串口設定](betaflight-setup.md)。
- 安全界線：不發送 raw RC、馬達、PID 或其他飛控設定命令。

## 6. 驗收條件

### Action 2 最低交付

- 第一次配對一次成功。
- 雙方各自重開機 10 次，不需清除配對、不需再次確認。
- 超距離斷線後回到範圍內能自行恢復。
- 重連後 BOOT 可連續完成開始／停止錄影 20 次。

### 公開 R SDK 最低交付

- Action 4 完成配對與重連回歸。
- BOOT 錄影切換 20 次無卡死。
- LCD 的模式、規格、電量與錄影時間與相機畫面一致。

### Betaflight 最低交付

- 驗證 API 版本門檻。
- USER1 啟用／停用各 20 次只產生一次對應錄影命令；通道可在 BF 改派而不改 ESP32。
- 兩行 OSD 可正確更新且每行不超過 16 bytes；未知／失聯資料不冒充有效狀態。
- 模組斷電、重啟或 BLE 斷線不影響飛控的飛行控制路徑。
