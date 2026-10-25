# 錄影與設定功能補完

目標來源：2026-10-22 使用者要求補完「錄影失敗提示 → ARM／停錄延遲 → 相機遙測與警告 → 設定便利性」。基準 `885c6a1`。先完成軟體與模擬驗證，不接觸實體序列埠；硬體驗收獨立記錄。

## 完成條件

- [x] 所有控制入口共用相機確認結果：等待開始／停止、開始失敗、未停止、錄影未知與意外停止，OSD／LCD／LED／管理工具一致；ACK 不等同錄影成功。
- [x] ARM 開始、DISARM 延遲 0–30 秒、重新 ARM 取消、分別開關自動開始／停止；保留 USER 模式並提供按鍵／混合／監看模式。處理初始狀態、晚連線、重連、手動覆寫、failsafe、時鐘溢位。
- [x] 待停止要求可在模組重啟後恢復；新命令取代舊要求，失聯不自行推測停錄。保存失敗要明示，不盲目重送 toggle。
- [x] Action 2 本機計時明示推算；GoPro 有官方協定證據的時間、容量、格式及警告；DJI R SDK 依實際可取得欄位呈現。每欄位都有有效性／能力判斷，不支援或過期不得冒充正常。
- [x] 低電量／剩餘錄影時間門檻、SD／模式／溫度與錄影風險提示，處理優先序、解除、未知值與連線中斷；不能以新增欄位但未接 adapter 算完成。
- [x] 新設定保存、舊版遷移、無效資料拒絕；桌面瀏覽器及 Python 管理工具能完整讀寫、讀回確認與解釋狀態。
- [x] 設定便利性：首次使用與飛前檢查、BF 配置讀回與 OSD 放置、可編輯 OSD、診斷匯出、移除配對／重設、手機地面 Wi-Fi 設定與更新，以及清楚的維護進出流程。任何 FC 設定寫入必須限於使用者明確的設定操作，不能在飛行背景修改。
- [x] 各層行為回歸、實際 GUI 模擬、C3/C6 建置、分區容量及更新包驗證、使用文件；逐條證據審核後才算完成。

新增相機品牌、Camera 2、KISS、GoPro preset／HiLight／GPS／尋機並不包含在這四項目標內。相機遙測僅以現有三條 adapter 可證實的協定為準，不能承諾取得相機未提供的值。

## 2026-10-25 最終軟體完成審核

使用者明確指定目前不接硬體、先完成軟體檢查。下列八項以目前工作目錄的實作、可重跑的主機測試、實際管理介面模擬和兩板建置作為完成範圍；不把模擬 NVS／BLE／HTTP／飛控當作硬體驗收。新增相機、雙相機與 CAMLINK 全功能複製仍不屬本次四項要求。

| 完成條件 | 實作與本次重新核對的證據 | 結果 |
| --- | --- | --- |
| 1. 共用錄影結果 | `camera_controller.c` 的 BOOT／自動入口共用 `recording_request` 與 `recording_feedback`；OSD／LCD／LED／INFO／管理頁使用相同結果。`recording_feedback_test.c` 驗證 ACK 不代表成功、12 秒未確認、延後確認、斷線未知、非預期停止、存檔與時鐘溢位；controller／dashboard／UI 回歸通過。 | 軟體通過 |
| 2. ARM 與多種模式 | `bf_control.c`／`bf_bridge.c` 接入 ARM、USER、Button、Combi、Off；`recording_automation_test.c` 驗證啟動已解鎖、晚連線、手動覆寫、重新 ARM、failsafe、0／30 秒邊界、獨立自動開始／停止與時鐘溢位。Button／Combi 使用 BF USER，不是直接 AUX 學習。 | 軟體通過 |
| 3. 待停止工作 | `management.c` 保存 `stop_v1`，bridge 恢復與持續保存，controller 以手動命令序號阻止舊工作覆寫。管理／controller 測試驗證重啟、相機／設定不符不繼承、保存失敗仍嘗試 STOP、START 清除失敗拒絕、派送失敗恢復舊要求。保存錯誤須成功寫入才能解除，讀到舊值不會消除警告。 | 軟體通過 |
| 4. 遙測能力 | `camera_telemetry.c` 的 Action 2 推算與 `~` 標記；GoPro 狀態／格式實際解析器；DJI R SDK 實際 callback。各自測試未知／過期／損壞／單位／模式，GoPro 選配查詢失敗退回基本路徑；已有錄影片段不猜起點。 | 軟體通過 |
| 5. 警告 | SD、模式、冷熱、寫入速度、低電量／時間依 validity 產生；`camera_telemetry_test.c` 驗證優先序、遲滯、解除、Link Pause 與未知值；錄影錯誤不被模板或計時遮住。各相機只提供已解析的欄位。 | 軟體通過 |
| 6. 設定保存與工具 | `settings.c`／`management.c` 的版本檢查、v1 遷移、4／8-byte 相容、current/saved；管理故障注入、25 項 Python、17 項 Web 協定測試及 Tk／實際瀏覽器確認保存與重啟後讀回，失配不報成功。 | 軟體通過 |
| 7. 設定便利性 | 首次使用／飛前提示、BF 身分／Modes／OSD 讀回與明確套用、七種 OSD token、診斷、配對清除／出廠重設、Wi-Fi 設定／更新／退出均有操作入口。7 項 BF Node 測試與實際頁面驗證寫前 ARM 檢查、未知協定／模式表拒絕、USB 恢復；重設逐 commit 故障注入通過。新增 `ground_wifi_test.c` 驗證真實 HTTP handlers、損壞／串接／最大封包、來源／token、逾時、互斥、一次性進入與啟動失敗；`boot_button_test.c` 驗證真實按鍵迴圈的 60／1200／8000ms、放開才觸發與時鐘溢位。 | 軟體通過 |
| 8. 建置、交付與文件 | 四組原生腳本、Python、Node 共 24 項、Tk，及 Web Serial／390px Wi-Fi／BF 實際 UI 模擬通過。C3／C6 ESP-IDF 5.5.5 建置成功；兩份最新 ZIP 均由 Python 與 JS 實際載入器驗證，與 bin 逐位元組一致。README、操作旅程、相機／BF／GoPro／維護文件已更新；本機文件連結與 diff 空白檢查通過。 | 軟體通過 |

重跑入口：`tools/test-management.ps1`（含 Wi-Fi／BOOT／重設／Python）、`tools/test-betaflight.ps1`、`tools/test-dji.ps1`、`tools/test-gopro.ps1`；原生腳本依序執行，避免共用 `build-host-tests` 物件互相覆蓋。Node：`node --test tests/test_web_configurator.mjs tests/test_bf_setup.mjs`。Tk：使用既有 ESP-IDF Python 執行 `tests/test_manager_gui.py`。瀏覽器重跑步驟見[設定器說明](../tools/web-configurator/README.md)。

最終映像：C3 `0x17ef70 / 0x1f0000`（23% 空間）；C6 `0x1f1610 / 0x3f0000`（51% 空間）。更新包為 `build-audit-c3/rotorrec-c3-update.zip` 與 `build-audit-c6/rotorrec-c6-update.zip`；完整雜湊與界線見[驗證紀錄](verification-status.md)。未刷寫、未提交 Git、未推送或發布。

尚待實機的項目明列於[操作旅程驗收表](user-journey.md)：實際 BLE／Wi-Fi、相機檔案、FC／OSD、按鍵與顯示、斷電保存、更新中斷與回滾，C3／C6 分別測試。Wi-Fi 模式停止 FC／BLE，不能用手機頁面配置飛控或對頻；BF 工具使用桌面 USB。這些操作限制已在介面與文件說明。

## 進度（歷史，最終結果以上表為準）

### 2026-10-25 接續驗證

- 已補上瀏覽器、Python CLI 與 Tk 的清配對／出廠重設入口。重設工作先保存、讀回，再重啟；有線需確認工作清除與儲存健康。Wi-Fi 出廠重設明示無法以原連線驗證。原生測試逐一注入每次 NVS commit 失敗，確認重啟可繼續、設定隔離與舊設定遷移。
- 修正 Python 新錄影設定重啟參數缺漏，加入真實 Client 重啟讀回測試。修正 ARM 維護判斷的分離原子狀態，未配置 USER 時改獨立讀 ARM；測試已知 ARM／failsafe／資料過期時拒絕 HELLO 且不啟動維護。
- Wi-Fi HTTP 傳輸測試通過遺失 ACK、相同封包重送、重啟 token 更新、取消與指紋確認；SHA-256 備援對照 Node 原生實作，涵蓋 padding 邊界及最大套件長度。熱點啟動失敗返回正常模式；退出排程不因重複呼叫延後，OTA 期間禁止按鍵退出。
- 實際瀏覽器的 Web Serial、390px Wi-Fi 及 BF 設定頁模擬均通過。BF 頁面驗證開埠失敗重試、預覽後 ARM 拒寫、位置保存讀回與 USB 例外恢復。Tk 測試亦涵蓋新能力解鎖、取消重設確認及清配對派送。全程無實體序列埠存取。
- 最新原生管理／重設、DJI、GoPro、BF／LCD／錄影／遙測測試通過；Python 25 項、Node 23 項、Tk 回歸通過。新增真實 controller 包裝層測試確認：保存 STOP 失敗仍嘗試停錄、START 清除舊 STOP 失敗即拒絕、舊序號無法覆蓋新手動命令或倒數。
- 最新兩板建置成功：C3 `0x17ef50 / 0x1f0000`（23% 空間）、C6 `0x1f1600 / 0x3f0000`（51% 空間）。兩個 `build-audit-*/rotorrec-*-update.zip` 已產生並由 JS 套件載入器核對內容、雜湊及 ELF 指紋。
- 尚未總體完成：需補 Wi-Fi HTTP 伺服端與 BOOT 手勢的原生故障邊界測試、完成文件一致性／連結核對及逐項完成審查。以上為目前版本證據；下列較早進度保留歷史用途，不表示本次缺口仍全部存在。實際 BLE／Wi-Fi 射頻、FC／相機、斷電與回滾仍另列硬體驗收。

### 較早實作進度

- 已實作共用錄影確認／失敗、ARM／Button／Combi／Off、自動起停開關、0–30 秒停錄延遲與重新 ARM 取消、手動覆寫、NVS 待停止工作、v2 設定保存與舊設定遷移。
- 已接通 GoPro 官方狀態查詢的錄影時間、剩餘時間、容量、SD／冷熱／寫入速度警告與解析度／FPS；選配查詢不支援時退回基本狀態。DJI R SDK 溫度欄位已接通。Action 2 只從確認的 OFF→ON 轉換推算時間，顯示 `~`，斷線或未知逾 5 秒便清除，不推算既有片段的起點。
- 遙測另用有界封包讀取，避免擠爆 INFO。OSD／LCD／LED／瀏覽器／Python 已接入錄影風險與有效性。四行 OSD 模板、NVS 保存與保存／重啟後讀回已接通；重要錯誤仍覆寫第一行。
- 已通過管理協定與 NVS／OTA 故障注入、23 項 Python、14 項 JS、11,966 項既有 BF 檢查與新增錄影／遙測／模板原生測試、Tk 舊版功能降級與重連測試。瀏覽器實際 Web Serial 模擬通過遙測、保存、配對、更新、取消、斷線恢復；OSD 模板新增後仍須重跑瀏覽器。
- 遙測版本已建置 C3 `0xdee10`／`0x1f0000`、C6 `0x13d960`／`0x3f0000` 成功；之後新增 OSD 與便利性功能，需於最終狀態再建置與打包。
- 設定便利性進行中：首次使用／飛前步驟、診斷 JSON、模板編輯已加入；獨立的 Betaflight MSP OSD 配置工具已實作，待完整模擬與文件驗證。手機 Wi-Fi、清配對／出廠重設、維護 ARM 限制與最終整合驗收尚未完成。以上總體完成條件因此仍未勾選。

## 協定依據

- GoPro [Query](https://gopro.github.io/OpenGoPro/docs/ble/query/)：`0x13` 空 ID 陣列讀取所有可用狀態，`0x12` 讀取設定 2／3；[官方 SDK 狀態定義](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/api/ble_statuses.py)及同版 `models/constants/settings.py`／`statuses.py`。容量由官方 KB 值除 1024 顯示為 MB，不表示位元組精度。
- DJI 隨附 SDK `camera_status_push_command_frame.temp_over`：0 正常、1 警告、2 禁止錄影、3 即將關機；沒有擅自從 Action 2 的原始未知位元推導 SD／溫度。
- Betaflight [OSD enum 與位置位元](https://github.com/betaflight/betaflight/blob/master/src/main/osd/osd.h)、[MSP 狀態及 OSD 讀寫](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp.c)：Custom Messages 索引 81–84、位置＋設定檔位元、讀取／設定／保存指令。獨立設定頁只在使用者檢查提案後操作，每次寫入前重新確認 ARM／FAILSAFE；韌體背景仍只有原有唯讀 MSP 與暫存 OSD 文字寫入。
