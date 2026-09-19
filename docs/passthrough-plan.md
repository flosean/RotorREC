# RotorREC：經 Betaflight 設定相機與更新韌體

日期：2026-09-19。狀態：已實作軟體與測試，實機驗證待完成。操作與目前限制見 [passthrough.md](passthrough.md)。

實作採 ponytail 簡化：保留 bf_bridge 為唯一 UART 擁有者，在既有讀取迴圈與等待期間解析管理封包，未另建 serial_link 或第二個 UART 任務。管理與 OTA 共用 management.c；逐塊擦寫避免長時間整區擦除，不另設 busy 封包。下列內容保留原始設計依據。

## 目標與決策

首次透過 ESP 板 USB-C 燒錄完整韌體；之後只接飛控 USB，即可選擇相機協定、重新配對、查看狀態及更新 RotorREC。使用既有 TX/RX/GND 與供電，不增加 BOOT/EN 控制線，不修改 Betaflight 韌體。

採用「Betaflight serialpassthrough + RotorREC 管理協定 + ESP-IDF 雙分區更新」。使用體驗類似 ELRS，但更新由執行中的 RotorREC 收檔，呼叫 esp_ota_* 寫入備用分區；不要求進入 ESP ROM downloader。OTA API 的資料來源可以是 UART，不需要 Wi-Fi。

第一版相機選擇指 DJI Action 2、DJI R SDK（目前 Action 4 路徑）、GoPro 協定。每種協定維持一個已配對目標；掃描結果清單及多相機收藏切換留待後續，不宣稱所有同品牌機型已支援。

## 現況與必改項目

| 現況 | 影響與改動 |
| --- | --- |
| UART1：C3 GP0/GP1；C6 GPIO18/GPIO19 | 保留接線；應用層接收更新，不能假設 ROM downloader 會使用這組脚位 |
| bf_bridge.c 自己讀 UART，exchange() 會 flush 輸入 | 抽出唯一 UART 接收者，避免吞掉管理握手；不能另開第二個 UART reader |
| Kconfig/CMake/camera_controller.c 編譯時二選一 DJI/GoPro | 同一板型映像納入所有支援 adapter，開機按 NVS 設定啟用其中之一 |
| DJI、GoPro 各自初始化 BLE 並註冊全域 callback | 每次開機只能啟動一個 adapter；切換協定後重啟 ESP，第一版不做熱切換 |
| single_app_large；未啟用 rollback | USB 首次燒錄須改成雙 OTA 分區與支援回退的 bootloader |
| DJI SDK 在特定 NVS 初始化錯誤時會 erase 全 NVS | 統一儲存初始化與錯誤處理，避免配對／更新流程意外清掉管理設定 |

目前已刷入舊版的板子必須先透過 USB-C 升級一次基礎版本，包括 bootloader、partition table、app。不能僅憑「已有我們的韌體」就讓舊版直接支援 passthrough 更新。

## 使用者流程

1. 關閉占用飛控串口的 Betaflight Configurator，開啟 RotorREC 管理工具。
2. 選飛控 COM 埠；工具讀取 BF 版本與串口配置。多個候選 UART 時由使用者指定，不盲目向接收機/GPS 等外設送探測封包。
3. 工具進入 BF CLI，對選定 UART 啟用固定 115200、全雙工 serialpassthrough；不寫入持久飛控設定。
4. 工具發送 RotorREC HELLO，ESP 暫停 MSP 輪詢／OSD 輸出並回覆板型、版本、相機選擇及更新能力。
5. 使用者選擇相機、儲存並重啟 ESP，或要求重新配對、查看配對碼與結果。配對所需的相機端確認仍保留。
6. 更新時選擇符合板型的更新包，顯示進度；完成校驗、重啟及版本確認後才顯示成功。
7. 完成後提示重新上電飛控以退出 passthrough。ESP 的退出管理指令不能直接讓飛控退出透傳。

工具要說明：部分飛控 USB 不會供電给該外設，需按實際供電配置處理。進入管理後暫停遙控開關控制，不主動停止相機既有錄影；錄影／存檔中不允許切換相機或更新。

## UART 與管理模式

新增 serial_link 作唯一 RX/TX 擁有者。MSP parser 與管理 parser 由其分派；bf_bridge 改為向此模組送請求、等回覆，不直接 flush/read UART。

狀態：NORMAL → MANAGEMENT → UPDATING → VERIFYING → REBOOT；另有取消及逾時返回路徑。

- NORMAL 同時辨識正常 MSP 回覆與完整管理 HELLO。握手需 magic、版本、長度、CRC、session nonce；一般 MSP 資料或雜訊不得切換模式。
- 工具在握手前容忍並忽略 ESP 既有 MSP 請求，有限次重試 HELLO；ESP 確認握手後停止產生 MSP 流量。
- MANAGEMENT 暫停 USER 自動控制、OSD 寫入，並抑制會干擾設定／更新的按鍵動作。查狀態及配對仍經 camera_controller。
- UPDATING 使用單一區塊在途、寫完才 ACK 的背壓；慢速 Flash erase 另回 busy/progress，工具不可用一般短逾時誤判。
- 斷線／逾時呼叫 esp_ota_abort，不修改啟動分區。第一版中斷後重傳整檔，無跨重啟續傳。
- 離開管理後清除舊 MSP 回覆與控制狀態、重新握手；維持現行「開關先回 LOW 才啟用」政策。飛控尚在透傳時保持控制無效。
- 開機先啟動管理通道，再初始化相機；BLE 失敗不得阻止設定與更新。重啟後提供短暫握手窗口，工具重連可保留管理流程。

封包提案：magic、protocol_version、command、session_id、sequence、payload_length、payload、CRC32。限制 payload 長度，支援分段／黏包／雜訊後重新同步。設定小封包與更新二進位資料共用同一 framing。

| 指令 | 用途 |
| --- | --- |
| HELLO / GET_INFO | 協定協商、板型、版本、能力、分區配置版本及更新容量 |
| GET_CONFIG / SET_CONFIG | 讀寫帶 schema_version 的相機設定；完整驗證後一次提交 |
| PAIR / GET_STATUS | 重新配對、配對碼、結果、實際錄影及電量 |
| UPDATE_BEGIN | 先驗證板型、chip、長度、分區布局版本及相容性，再開啟備用分區 |
| UPDATE_DATA | offset、序號、資料；重送相同區塊只 ACK，不重複寫入；亂序拒絕 |
| UPDATE_END | 比對完整長度、SHA-256、esp_ota_end 驗證，最後才切換下次啟動分區 |
| UPDATE_ABORT / REBOOT / EXIT | 中止、重啟 ESP、結束管理；區分 ESP 與飛控生命週期 |

CRC／SHA-256 用於損壞檢查，並非發布者身分認證。第一版使用受信任本機更新包；若未來做線上發布通道，再增加簽章驗證，不以雜湊假稱簽章。

## 相機設定與儲存

- camera_controller 以小型 switch 分派既有 adapters，不建立通用 plugin framework。
- NVS 儲存 schema_version、camera_type；各協定配對資料保持各自 namespace。切換品牌不抹除另一品牌配對資料。
- 相機協定切換：驗證 → 提交 NVS → 回覆需重啟 → 工具要求重啟 → 新 adapter 初始化。避免兩套 BLE callback 同時註冊。
- 明確選 Action 2 時直接走 Action 2 配對；選 DJI R SDK 時不自動降級為 Action 2。現有 DJI 自動探測是否保留為獨立 Auto 選項，待相機回歸驗證後決定。
- 預設 Action 2 對應目前主要驗證路徑；GoPro 顯示「硬體驗證待完成」。
- 舊 NVS 可無損載入；新版本確認有效前，不做舊韌體不可讀的破壞性遷移，確保回退仍可工作。
- 第一版不把 UART GPIO／baud 暴露為使用者設定，避免設定後失去唯一管理通道。

## 更新與 Flash 配置

每板型各一個韌體，內含所有相機 adapters；C3/C6 的映像仍分開。初始安裝包含 bootloader、分區表與 app；後續包只含 app 及 manifest，不透傳改写分區表／bootloader。

使用 nvs、otadata、ota_0、ota_1，依實際配置保留其他必要 data 分區。C3 4MB 是先決容量檢查：合併 DJI+GoPro 後實測映像大小、堆積記憶體及連結結果，再固定兩個等大且對齊的 OTA slot；每個 slot 預留成長空間。C6 8MB 同樣建立獨立配置。

若 C3 的統一映像無法放進雙 slot，先移除無用編譯功能並量測；若仍不夠，應改用較大 Flash 硬體，或明確縮小相機支援範圍。不能默默退回單分區覆寫而保留「可回退」承諾。

manifest 至少包含 product、board_id、chip、firmware_version、management_protocol_version、partition_layout_version、image_size、sha256。板型與 app 內嵌描述交叉比對，不能只相信檔名或僅檢查同為 C3/C6。

啟用 CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE。新 app 首啟以 UART 管理服務、NVS 讀取、必要任務及內部自檢判定健康；不要求附近有相機或 BF 已退出透傳。確認後才 mark valid。崩潰／未確認重啟由 bootloader 回退；自檢卡住要有 watchdog／有界逾時觸發重啟，不能假設永遠卡住也會自己回退。

115200、8N1 的理論上限約 11.52 kB/s，1 MiB 約 91 秒，實際含 ACK、擦寫會更久；工具顯示真實吞吐與進度。高速傳輸等低速全流程可靠後再評估。

回退保護不能救所有損壞：分區表、bootloader 或兩個 app 都損壞時仍需 USB-C 救援。

## 分階段交付與驗收

| 階段 | 交付 | 通過條件 |
| --- | --- | --- |
| 0 可行性 | 兩板型統一編譯試驗、映像/RAM 量測、UART 收包設計 | C3 雙 slot 可行、BLE callback 無衝突；列出測試用 FC/BF 版本 |
| 1 管理通道 | serial_link、framing、HELLO、GET_INFO；本機 CLI 工具 | 經真實 BF passthrough 讀出資訊；不再被 flush 吞包；原 MSP/OSD 回歸通過 |
| 2 相機選擇 | NVS 設定、開機 adapter 分派、配對/狀態 | 不重刷即可切 DJI/GoPro；重啟後保存；各相機實機回歸分開記錄 |
| 3 可回退更新 | 分區表、OTA receiver、manifest、工具傳輸 | 更新、斷電、壞檔、錯板型、回退及更新後再次更新全部通過 |
| 4 使用者介面 | 管理工具的連線、相機、更新三個區域；安裝／更新包 | 使用者不需手打 BF CLI；清楚顯示配對確認、重啟與重新上電需求 |

第一個工具採 Python + pyserial CLI，先驗證 BF 與韌體流程；之後包裝 UI。若選瀏覽器 Web Serial，另驗證瀏覽器支援、埠權限及 USB 重連，不在本提案預設必須網站部署。

預計改動：main/betaflight/bf_bridge.c、main/camera/camera_controller.c、main/app_main.c、main/CMakeLists.txt、main/Kconfig.projbuild、兩板 defaults；新增 main/serial/、main/management/、main/update/、板型分區 CSV 與 tools/rotorrec_manager.py。模組以實際責任切分，不先做多餘抽象。

必要測試：封包半包／黏包／CRC／超長／逾時；MSP 與 HELLO 交錯；重送區塊不重複寫；錯 offset、截斷、錯映像及超容量；取消與關工具；寫入中／切換啟動分區時／新 app 首啟時斷電；新 app 崩潰回退；NVS 回退相容；更新後連續再更新；配對與錄影回歸。實機測試覆蓋 C3/C6 與記錄過版本的飛控，未跑的項目標記待驗證。

## 參考依據

- [Betaflight Serial](https://betaflight.com/docs/wiki/guides/current/Serial)：透傳、埠編號、baud 與重新上電退出。
- [ESP-IDF v5.5 C3 OTA](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-reference/system/ota.html)：雙 app slot、otadata、驗證與 rollback。專案實作仍須對 v5.5.5 安裝版本確認 API。
- [ELRS Passthrough](https://www.expresslrs.org/software/updating/betaflight-passthrough/)：使用流程參考，不直接套用 ELRS 的韌體目標／刷寫協定。
