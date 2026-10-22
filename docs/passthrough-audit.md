# Betaflight Passthrough 與 Chrome 設定工具檢查

日期：2026-10-22。版本：1.2.0-dev。

結論：現有韌體已具備 UART 管理、相機切換、設定保存、雙槽 OTA 與首次啟動回退。本次補齊主機端錯誤處理與 Chrome / Edge Web Serial GUI。軟體測試及兩板編譯通過；實際飛控透傳、相機與斷電回退仍須完成實機驗收，不能把模擬測試視為硬體結果。

## 流程檢查

| 流程 | 檢查結果 |
| --- | --- |
| 首次安裝 | C3 / C6 各有雙 OTA 分區、otadata 與 rollback；舊 1.0 單槽版仍需 ESP USB 安裝一次完整布局 |
| 飛控連線 | 識別 Betaflight、讀取 UART 配置，只選指定或唯一 MSP UART，要求 115200；不探測其他周邊 |
| UART 管理 | bf_bridge 是唯一讀取者；RREC v1 包含 CRC、session、sequence、長度限制、重送快取、部分封包與連線逾時 |
| 相機與設定 | Action 2 / DJI R SDK / GoPro；配對及 USER1–4、OSD 2/4 行、LED 亮度、電量警告、Link Pause；保存後重啟讀回確認 |
| 更新包 | 只接受 manifest.json + app.bin；檢查板型、晶片、描述、版本、大小、SHA-256 及 ELF 指紋 |
| 傳輸 | 寫入備用槽，1024-byte stop-and-wait、相同封包重送不重複寫入；取消不提交啟動槽 |
| 更新完成 | 韌體驗證完整 hash 與 ESP 映像後切換啟動槽；工具確認重啟、版本、ELF 指紋與 pending_verify 結束 |
| 回復正常控制 | 關閉管理連線後仍需將飛控重新上電以退出 passthrough；僅重啟 ESP 不足 |

## 本次補齊

- 修正 Python CLI 把分段回覆中的 `#` 或 `# ` 當作完成提示符，導致 Betaflight 誤判；瀏覽器採相同完整提示符及空閒讀取處理。
- 最終 END 回應全部遺失時，改以新 session 等待裝置逾時自動重啟，再確認映像；不向舊 session 發送模糊的 ABORT。
- 新映像曾回覆 HELLO、但尚未確認便重啟時，重新握手並偵測回退；相同版本但不同 ELF 指紋不算成功。
- 新增 Chrome GUI：連線、狀態、配對、相機協定、基本設定、更新包驗證、刷寫、進度、取消及操作紀錄。
- 錯誤更新包保留已連接裝置；UART 或埠變更清除自動記住的透傳狀態；同步成功的取消會釋放 session，未知結果則等待逾時重新確認。

## 驗證

- `tools/test-management.ps1`：實際 C receiver / state machine 的 NVS、OTA 故障注入通過；Python manager **20 項測試通過**。
- `tools/test-betaflight.ps1`：**11,965 個檢查通過**，含 LCD 文字回歸。
- `tools/test-dji.ps1`、`tools/test-gopro.ps1`：通過。
- `tests/test_manager_gui.py`：既有隱藏 Tk GUI 回歸通過；使用已安裝 pyserial 的 ESP-IDF Python。
- `node --test tests/test_web_configurator.mjs`：**11 項測試通過**，涵蓋跨語言 wire golden、分段、CRC、ZIP、設定、ACK 遺失、取消、回退與串流清理。
- `tests/web-configurator-ui.html`：實際 GUI 搭配模擬 Web Serial streams，驗證相機與設定保存、配對、檔案輸入、更新、取消、USB 讀取失敗與斷線。以專案根目錄啟動本機 HTTP server 後開啟測試頁；不存取實體串口。
- Chromium 瀏覽器載入兩個實際編譯的 ZIP，均顯示 SHA-256 驗證成功；錯誤檔案遭拒，檢查時無 console error / warning。
- ESP-IDF v5.5.5 C3 / C6 編譯成功；C3 app **903,648 / 2,031,616 bytes**，C6 app **1,290,688 / 4,128,768 bytes**。工具鏈有缺少 `ESP_ROM_ELF_DIR` 的除錯符號警告，不影響產生韌體；這不代表已驗證硬體除錯功能。

更新包位於本機 `build-passthrough-c3/rotorrec-c3-update.zip` 與 `build-passthrough-c6/rotorrec-c6-update.zip`。首次安裝所需的 bootloader / partition table / app 仍使用現有 [USB 安裝流程](build-and-flash.md)。

## 實機待驗收

兩板都需記錄 FC 型號、Betaflight 版本、相機型號與韌體，完成：第一次 USB 安裝保留設定、透傳進出、所有相機選擇及配對、保存後重啟、兩槽輪流更新、傳輸時斷線／斷電、提交及首次開機斷電、刻意崩潰觸發 rollback、取消後重連，以及重新上電後 USER / OSD 控制。詳見 [驗收清單](passthrough.md)。本次沒有刷寫或操作實體裝置。

操作入口：[Chrome 設定工具](../tools/web-configurator/README.md)。協定依據：[Betaflight CLI 原始碼](https://github.com/betaflight/betaflight/blob/4.5.2/src/main/cli/cli.c)、[ESP-IDF 5.5 OTA](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-reference/system/ota.html)、[Chrome Web Serial](https://developer.chrome.com/docs/capabilities/serial)。
