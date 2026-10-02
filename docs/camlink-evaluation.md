# CAMLINK 功能參考與 BF_Cam 導入評估

查核日期：2026-10-01。以下保留當時的功能評估與程式狀態；未操作 CAMLINK 實機。2026-10-02 已依使用者指示實作首批功能，現況與驗收請看 [1.2 設定與 Link Pause](camera-settings.md) 及 [驗證紀錄](verification-status.md)。

建議先完成 RotorREC 1.1 硬體回歸，再加入 **四行 OSD、可保存設定、低電量提示與 Link Pause**。Wi-Fi 地面設定頁與可選 ARM／延遲停錄可各自作為下一個里程碑。新品牌、KISS、跨品牌自動辨識及自動修改 OSD 位置暫緩。

我們已有相機控制、電量／錄影 OSD、配對重連與 UART 更新，不需從頭重做；DJI R SDK 也已有部分額外遙測。文末列出程式缺口、導入條件與驗收。優先序都是提案，尚未改動現行產品需求。

## 官方功能與來源

以下為廠商文件宣稱，**不是本專案的相容性承諾或實測結果**。主要依據使用者指定的 [Quickstart](https://itsfpv.de/en-int/pages/camlink-quickstart)，頁面標示 firmware 1.0.0、revision 2026-09-15；逐型號資料取自同頁的 `CL-COMPAT` 資料，版本 `1.0.0 / 7278dbb8`。瀏覽工具讀取 en-int 時失敗，但直接 HTTP 已取得官方完整頁面；未引用二手介紹。

### 功能清單

| 項目 | 官方宣稱與評估時必須保留的限制 | 來源 |
|---|---|---|
| 連線 | 四線 UART/MSP 接 FC；Bluetooth 一次連一台相機。換品牌需重新配對，不需更改品牌設定；成功新配對替換原配對。 | [配對](https://itsfpv.de/en-int/pages/camlink-quickstart#pairing) |
| 錄影觸發 | `Arm`、`Switch`、`Button`、`Combi`、`Off`。Combi 支援 ARM 自動開始及 AUX 瞬時按鈕切換；ARM 中手動停止後，不應立即被自動開始覆蓋。 | [Recording](https://itsfpv.de/en-int/pages/camlink-quickstart#recording) |
| 自動開始／停止 | Arm/Combi 的 arm-start、disarm-stop 可各自開關；ARM 中晚連線／重連也可開始。持續同一需求的重試間隔至少 20 秒；命令送出不等於相機已開始。 | [Recording](https://itsfpv.de/en-int/pages/camlink-quickstart#recording) |
| Stop delay | 0–30 秒，預設 5 秒；只套用 Arm/Combi 的自動 disarm-stop。重新 ARM 取消倒數；AUX 直接停止跳過延遲。Combi 倒數中再按按鈕可取消停止。 | [Recording](https://itsfpv.de/en-int/pages/camlink-quickstart#recording) |
| Failsafe | Switch 與 preset-switch 採 FC 回報的 AUX 位置；Button/Combi/HiLight 忽略 failsafe 中 AUX 變化。因此 Switch 可能因 failsafe AUX 位置而停止錄影。 | [Recording](https://itsfpv.de/en-int/pages/camlink-quickstart#recording) |
| Link Pause | 停止相機搜尋、斷開 BLE、清空四行相機 OSD；保留配對、飛行 OSD，以及換電後的暫停狀態。解除後重連，不必重配。 | [Link Pause](https://itsfpv.de/en-int/pages/camlink-quickstart#link-pause) |
| OSD 編輯 | 四行，每行 16 字元、最多八個 token；Clean/Standard/Detailed 與拖放排序。未確認／不支援值留空。警告暫佔第一行、狀態可佔第二行。 | [OSD](https://itsfpv.de/en-int/pages/camlink-quickstart#osd) |
| 警告 | 七種可選警告預設關閉：過熱、過冷、模式不對、等待 GPS、無卡／卡滿、空間不足、電池低；後兩者有門檻。卡故障、錄影命令失敗、連線狀態不依可選開關。 | [Warnings](https://itsfpv.de/en-int/pages/camlink-quickstart#warnings-deep) |
| Setup Wi-Fi | 地面設定模式；單一用戶端、無網際網路／帳號要求，本機 `10.0.0.1`。可改 SSID/密碼；大部分設定自動儲存，Wi-Fi 需明確儲存並重啟套用；此模式暫停相機控制／配對。 | [Setup Mode](https://itsfpv.de/en-int/pages/camlink-quickstart#setup-mode) |
| 設定與診斷 | 飛前狀態總覽、AUX 即時學習與 Low/Mid/High 範圍、MSP Connected/Silent/Degraded、baud、軟體交換 RX/TX、重啟／重設。診斷檔供廠商解密，Wi-Fi 密碼遮罩。 | [設定／支援](https://itsfpv.de/en-int/pages/camlink-quickstart) |
| OSD 自動配置 | Betaflight 可啟用／放置四個 Custom Message 並讀回驗證；只提供左上／左下，不會避開既有元素；須 disarmed。KISS 需手動配置 Custom Text。 | [Flight controller](https://itsfpv.de/en-int/pages/camlink-quickstart#betaflight) |
| 按鈕／LED | 配對、Link Pause、Setup、factory reset；ARM 或錄影時禁止實體按鈕。LED 顏色與閃爍表示狀態，有 5/25/100% 亮度，不能關閉。 | [Buttons/LED](https://itsfpv.de/en-int/pages/camlink-quickstart#buttons-led) |
| 更新 | Wi-Fi 上傳簽署 `.camfw`，拒絕 raw bin／異裝置套件；有階段進度、重啟與啟動失敗回滾，中斷需重新上傳。頁面最新列 1.0.0（2026-09-01）。 | [Firmware updates](https://itsfpv.de/en-int/pages/camlink-quickstart#updates) |

### 相機支援必須分「已驗證／預期／未測試」

`已驗證` 是 **itsFPV 廠商** bench 驗證；`預期` 是廠商以控制協定相同推估，不能當成逐型號測過。下表只列資料中的機型，不能推廣至全品牌。來源：[官方相機選擇器與同頁相容資料](https://itsfpv.de/en-int/pages/camlink-quickstart#cameras)。

| 品牌 | 廠商已驗證 | 廠商預期但未 bench 驗證 | 未測試／其他限制 |
|---|---|---|---|
| GoPro | HERO 11、HERO 13、MAX 2、Mission 1 Pro | HERO 9/10/12、Mission 1、Mission 1 ILS | Mission 系列無 PROFILE；勿推定所有 HERO／原 MAX 都可用。 |
| DJI | Osmo Nano、Osmo Action 2、Osmo Action 6 | Osmo Action 4、Osmo Action 5 Pro | Osmo 360 列未測試、可用 token 為零；Action 3 僅留未測試說明、未列模型清單。 |
| Blackmagic | Pocket Cinema Camera 4K、6K | Pocket Cinema Camera 6K Pro、6K G2 | 相機 firmware ≥8.1。 |
| Sony | ZV-E10 II、ZV-E1 | 相容資料列出的其他 Sony 型號，見下 | 需 Bluetooth Function + Bluetooth Rmt Ctrl；MOVIE 自訂按鍵仍須指向錄影。 |

Sony 廠商「預期」清單：A1 II/A1、A9 III/II/A9、A7 V/IV/III、A7S III、A7R V/IVA/IV/IIIA/III、A7CR、A7C II/A7C、A6700/A6600/A6400/A6100、ZV-E10、ZV-1 II/1F/1A/1、FX30/FX3A/FX3/FX2、RX1R III、RX100 VIIA/VII、RX0 II、ILX-LR1。[官方相機選擇器](https://itsfpv.de/en-int/pages/camlink-quickstart#cameras)

### OSD token 與品牌差異

以下是廠商能力表摘要；值仍需相機回報、解析及有效性確認，不能直接用品牌名稱填入。[官方 token reference](https://itsfpv.de/en-int/pages/camlink-quickstart#osd)

| 能力 | 官方列出的品牌／限制 |
|---|---|
| `STATE`、`REC_TIME`、`BATTERY`、`REMAINING`、`CAM` | 四品牌；Sony 若連線前已錄影，該段 timer 留空。Action 2 不提供 elapsed，模組從已確認開始計時，斷線超過一分鐘清除 timer。 |
| `RES`、`FPS`、`ASPECT` | GoPro／DJI／Blackmagic；Sony 無。 |
| `SD` | GoPro／DJI；Sony／Blackmagic 以卡狀態警告與錄影剩餘時間替代容量 token。 |
| `PROFILE`、`BUSY`、`PRESET`、`TC` | GoPro 專屬；Mission 無 PROFILE；preset 最多 12 字元；TC 表示鐘錶同步成功。 |
| `ISO`、`SHUT_ANG`、`SHUT_SPD`、`WB`、`IRIS`、`CODEC` | Blackmagic 專屬。 |

DJI Action 4–6 不提供獨立 missing-card 狀態，不能由 free-space 為零直接聲稱「未插卡」。`!NO SD` 在 DJI 僅 Nano/Action 2；過熱為 GoPro/DJI，過冷僅 GoPro。狀態過期顯示 `!CAM STALE` 並隱藏舊值；Sony 命令已接受但未確認錄影可顯示 `!REC UNCONFIRMED`。[官方 warnings](https://itsfpv.de/en-int/pages/camlink-quickstart#warnings-deep)、[OSD 狀態](https://itsfpv.de/en-int/pages/camlink-quickstart#osd)

### GoPro 專屬功能與重要例外

| 功能 | 官方宣稱／限制 | 來源 |
|---|---|---|
| Auto wake | 預設開啟，開機喚醒已配 GoPro，可關閉；**DJI 無 wake**。 | [Auto wake](https://itsfpv.de/en-int/pages/camlink-quickstart#gopro) |
| Preset AUX 切換 | Low/Mid/可選 High；位置穩定 ≥0.5 秒才載入。錄影中預設延後套用；可選先確認停止、載入、仍有錄影需求時重開新片。 | [Preset](https://itsfpv.de/en-int/pages/camlink-quickstart#gopro) |
| Preset 記憶 | 按相機保存最多四份映射，切換其他品牌保留；第五份淘汰舊項，優先沒有 mapping 的項目。識別含相機序號，並非只按型號。 | [Saved presets](https://itsfpv.de/en-int/pages/camlink-quickstart#gopro) |
| HiLight | 錄影中新增標記，獨立瞬時 AUX，不與其他功能共用；成功由相機確認；後製軟體須支援。 | [HiLight](https://itsfpv.de/en-int/pages/camlink-quickstart#gopro) |
| GPS 時鐘 | 預設關閉，有效 GPS fix + 時區，寫入後讀回確認；**不是 frame-accurate timecode**。儲存 UTC offset 不會自動跟隨旅行／DST。 | [Clock](https://itsfpv.de/en-int/pages/camlink-quickstart#gopro) |
| Naked pairing | GoPro Labs QR 配對，須在拆機前裝 Labs；屬選配工作流。 | [Naked GoPro](https://itsfpv.de/en-int/pages/camlink-quickstart#gopro-naked) |

### 官方文件衝突與外部限制

- 本次完整 Quickstart 明確寫 Betaflight 2025.12+／KISS Ultra B67+，並含 KISS 配置。官方 [en-de 產品頁](https://itsfpv.de/en-de/products/camlink) 的本次瀏覽回應另稱不支援 KISS／其他 FC firmware，與指南衝突。以指定指南評估功能，但 **KISS 實際支援仍待廠商確認**，不能據此宣稱 BF_Cam 已支援。[Quickstart](https://itsfpv.de/en-int/pages/camlink-quickstart)
- DJI 不可由 CAMLINK 喚醒／延長自動關機，Nano 最長 30 分鐘；配對前須醒著。Nano/Action 2 約 30 秒內在相機確認碼；Blackmagic 用搖桿輸入 PIN；Sony 需相機端接受。[配對與相機限制](https://itsfpv.de/en-int/pages/camlink-quickstart#pairing)
- BLE 斷線本身不會停止已在錄影的相機。相機使用獨立電池時，模組失電也不會停止錄影；共用機載電源則拔電會一起斷電。停止命令與 `STP` 均不代表錄影已結束。[Recording/Buttons](https://itsfpv.de/en-int/pages/camlink-quickstart#buttons-led)
- 相機 API 可用性、DJI 欄位、GoPro wake/預設切換，不因競品列出就證明本專案可用。導入前須按本專案協定實作與 exact-model 實測建立自己的能力表。

## RotorREC 程式對照與加入評估

本節依 2026-10-01 的工作區程式（基準 commit `83c3bf1`）和驗證文件評估。下列優先序是提案，沒有啟用新韌體功能。難度「低／中／高」表示相對開發與驗證負擔，不是工期承諾。

### 已有基礎與實際缺口

| 功能 | RotorREC 現況與證據 | 評估／優先序 |
| --- | --- | --- |
| 錄影開關、真實狀態及電量 | 已有 USER1–4、BOOT、錄影回報和電量；USER 編號目前由 Kconfig 編譯設定，AUX 分配在 BF Modes。[控制政策](../main/betaflight/bf_control.c)、[Kconfig](../main/Kconfig.projbuild) | 保留；不需重做。先完成目前版本硬體回歸 |
| 配對、重連、跨品牌切換 | 三個協定都在同一映像；UART 管理器選協定，NVS 保存後重啟；各 adapter 有獨立保存目標。[控制器](../main/camera/camera_controller.c)、[管理器](../main/management/management.c)、[操作文件](passthrough.md) | 已有手動切換；自動辨識屬新功能。保留舊品牌配對資料較符合我們現況 |
| 四行 OSD | 封包建構器支援四行，每行 16 bytes；實際 bridge、快取及 formatter 只使用兩行。[封包介面](../main/betaflight/osd_messages.h)、[bridge](../main/betaflight/bf_bridge.c)、[formatter](../main/betaflight/bf_control.c) | **優先加入，中**。先提供固定的二／四行範本，再做欄位選擇；勿覆寫未授權使用的第 3、4 行 |
| 拍攝設定、錄影時間、剩餘時間／容量 | DJI R SDK 已填入 snapshot；Action 2 僅電量輸出，02/80 候選欄位只供診斷；GoPro 目前只查 busy/encoding/battery/ready/preset group。[R SDK](../main/camera/dji/dji_rs_sdk.c)、[Action 2](../main/camera/dji/dji_action2.c)、[GoPro](../main/camera/gopro/gopro.c) | **優先分協定加入，中**。R SDK 可先接顯示；Action 2、GoPro 各自補協定或本地估時，不能宣稱同等支援 |
| Action 2 錄影計時 | 未實作；未知封包未作有效值。[共享狀態](../main/camera/camera_types.h)、[控制器](../main/camera/camera_controller.c) | **可加入，中**。以確認的錄影起點計時，標明本地估算；重連時已在錄影但起點未知，顯示未知 |
| 低電量／儲存／過熱／低溫警告 | 有電量、saving 和不明狀態提示；沒有通用 warning 欄位或閾值設定。[OSD](../main/betaflight/bf_control.c)、[GoPro parser](../main/camera/gopro/gopro_protocol.c) | **優先加入，中**。先做有效電量的低電量警告；儲存／溫度警告依相機能力補上 |
| Link Pause，不帶相機時暫停搜尋 | 無共用暫停／恢復 API；management 會暫停 USER 控制，但不是 Link Pause。[控制器介面](../main/camera/camera_controller.h)、[bridge](../main/betaflight/bf_bridge.c) | **建議加入，中**。暫停搜尋／重連、保留配對、清除本模組擁有的 OSD 行；恢復後重新確認狀態 |
| ARM／Switch／Combi | 只有 USER 電平政策；模式映射僅接受 ID40–43，沒有 ARM 欄位，也沒有 momentary latch。[模式解析／政策](../main/betaflight/bf_control.c) | **後續可選，中**。先加 ARM，再評估 momentary／Combi；維持 USER 為預設，屬現有需求的擴充 |
| Disarm 後延遲停錄 | 無倒數；USER 下降沿經 debounce、相機就緒後要求停止。[政策](../main/betaflight/bf_control.c) | **後續可選，中**。與 ARM 模式一起做；重新 ARM 取消停錄。舊 USER 預設停錄延遲維持 0 |
| Wi-Fi 本機設定頁 | 無 Wi-Fi／HTTP app 模組，現有管理介面為 Python GUI／CLI。[建置依賴](../main/CMakeLists.txt)、[管理文件](passthrough.md) | **值得加入，高**。先做地面設定頁，UART 保留為配對／更新／恢復路徑；不先做手機 App |
| LED 日／夜亮度 | C3 RGB 使用固定低亮度，無保存設定；C6 的 LCD 是另一個硬體介面。[LED 模式](../main/ui/status_indicator.c)、[LED driver](../main/platform/status_led.c) | **可先加入，低**。以既有亮度為預設，NVS 存檔後統一縮放；C6 背光另外評估 |
| GoPro 預設切換、相機喚醒 | 目前只讀 preset group，要求 Video；沒有使用者可選 wake 政策。[GoPro](gopro.md) | **延後，中～高**。先完成 GoPro 基本硬體驗證；喚醒只能在仍有 BLE 廣播且機型支援時保證 |
| GoPro HiLight／GPS 時鐘同步 | 沒有 HiLight 指令或 FC GPS／時間讀取通路；現有 MSP allowlist 僅 API、variant、mode/status 和 OSD。[bridge](../main/betaflight/bf_bridge.c)、[GoPro adapter](../main/camera/gopro/gopro.c) | **延後，中～高**。HiLight 可獨立評估；GPS 時鐘同步需新增讀取／設定／讀回驗證，不當成精準 timecode |
| 無畫面 GoPro 配對教學 | 現有 GoPro 文件只說進入配對模式。[GoPro 指南](gopro.md) | **可補文件，低**。QR／GoPro Labs 是相機端準備流程，不是 RotorREC 韌體能力；需逐型號確認 |
| 更新套件、雙槽與回滾 | 已有 UART 更新 ZIP、SHA-256、板型／映像檢查和雙槽；沒有發佈者簽章。[更新協定](../main/management/management.c)、[更新文件](passthrough.md) | **沿用現有核心**。Wi-Fi OTA 只是新增入口；簽章驗證與發佈金鑰管理另列里程碑，hash 不能代替簽章 |
| 安裝診斷、相容性矩陣 | 管理 INFO 有版本／板型／相機狀態；FC/API／控制抑制原因尚未完整輸出；已有驗證狀態文件。[管理器](../main/management/management.c)、[驗證紀錄](verification-status.md) | **優先加入，低～中**。顯示 FC 連線、API、模式映射、資料新鮮度；逐型號／韌體記錄實測 |
| 自動 OSD 位置設定 | 沒有；目前允許的 FC 寫入僅 RAM OSD 文字。[MSP allowlist](../main/betaflight/bf_bridge.c) | **暫緩，高**。會擴大 FC 設定寫入範圍；先提供 Configurator 手動放置教學 |
| Sony／Blackmagic／KISS Ultra | 沒有 adapter；目前限定 BTFL，產品需求將前兩品牌列範圍外。[需求](requirements.md)、[bridge](../main/betaflight/bf_bridge.c) | **獨立產品擴充，高**。先確認使用需求與硬體，再新增 adapter；不能視為小修改 |

### 實作前必須定義的行為

**1. 每個遙測欄位都有自己的有效性。** 目前 snapshot 的單一 `valid` 加上 recording/battery flags，不足以表達「有錄影狀態但沒有容量」。增加每協定／機型能力及各欄位有效性、時間戳與來源（相機回報或本地估算），以明確單位保存。未支援、過期、未知與真正的零值須分開。R SDK 的容量／時間也要對照相機實測，不能只因 struct 存在就標已驗證。

GoPro 官方列出錄影時間 13、剩餘錄影時間 35、卡片剩餘 54、過熱 6、低溫 85、SD 寫入速度錯誤 111 和 SD 錯誤 112，適合依能力逐步增加；不代表每個機型均回覆所有欄位。可選遙測查詢失敗不能使既有錄影／電量基本查詢整體失效。[Open GoPro 狀態規格](https://gopro.github.io/OpenGoPro/docs/ble/statuses/)。R SDK 另依官方欄位單位核對。[DJI 官方資料規格](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/docs/protocol_data_segment.md)

**2. 擴充 OSD，不隱藏錄影的不確定性。** 初版可選「現有兩行」或「四行」：狀態／時間、電量／剩餘時間、解析度／FPS、相機／容量。此為版面提案，不是每個相機都提供這些值。每行限制 16 bytes，英文 ASCII 輸出以配合目前協定；資料未知用 `--`。警告須保留實際錄影狀態，例如 `REC HOT`，避免警告蓋掉「錄影是否確認」。儲存與電量閾值加遲滯，防止提示閃爍。關閉四行模式時清空先前由本模組啟用的行。

**3. Link Pause 保留配對，避免默默丟棄操作。** 補共用 pause/resume 介面，讓所有 adapter 停止掃描、重連與 keepalive，斷開連線並使舊遙測失效。初版只允許確認未錄影、未 saving、無 pending command 時進入；相機狀態未知且仍連線時拒絕。相機完全離線時可進入。建議保存暫停設定，換電後仍保持，透過 LED／管理器明確標示並提供恢復入口。退出重新取得有效資料，USER 控制重新等待 inactive。不要把 Link Pause 實作成清除配對或 STOP 指令。

**4. ARM 與延遲停錄須獨立於鏈路故障。** 擴充模式映射時另讀 ARM，不把 USER 的 ID 限制改成任意 mode 後沿用。使用非阻塞 deadline，重新 ARM／手動開始可取消待停錄；UART、接收機或相機鏈路失效不產生 STOP，也不在恢復時執行過期倒數。每次開機／恢復仍需有效基準狀態，不能把「讀不到 ARM」當成 disarm。Combi 要先定義手動操作、ARM 邊緣與倒數的優先權。這會擴充 [目前明訂不使用 ARM 的需求](requirements.md)，需在實作時一併修訂規格。

**5. Wi-Fi 先做與飛行分離的設定模式。** ESP-IDF 5.5.5 的 C3 共存表將 SoftAP 有客戶端連線與 BLE 的組合標為 C1（可支援但效能不穩定），不能僅因晶片有 Wi-Fi 就承諾手機連線期間相機控制穩定。[Espressif C3 共存文件](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c3/api-guides/coexist.html)；C6 也必須另外量測。[C6 共存文件](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-guides/coexist.html)

MVP 建議：明確的地面入口、Wi-Fi 預設關閉、停止 BLE 後啟動本機 AP，設定寫入 NVS，退出重啟並恢復飛行模式；配對仍透過 BOOT 或 UART。頁面可做相機協定選擇、USER 編號、OSD 範本、LED、韌體版本和更新。初版不提供一邊連 AP 一邊配對的流程。線上入口需有效的未 ARM 狀態和相機 idle；FC 失聯的恢復入口要獨立設計，不能以失聯冒充地面狀態。現有 BOOT 短按／1.2 秒長按已占用，須明訂新的開機按住或更長按住流程，不能直接複製對方的第二顆按鈕。

**6. 管理與 OTA 共用核心，傳輸各自處理。** 現有 management.c 同時處理 UART session、NVS 和 OTA，直接由 HTTP 呼叫 UART receiver 會碰到 session／UART 所有權問題。僅在加入第二入口時抽出共用設定／OTA 操作，保留一個更新擁有者和相同 idle gate。網頁資源先內嵌 app，不先新增檔案系統／改 partition。C3 的 4 MB 和雙槽限制需重新量測 app 大小、最小 free heap、最大連續可用區塊與各 task stack；[目前驗證文件](verification-status.md) 的 linker RAM 數字不能當成 BLE＋HTTP 運行時剩餘 heap。新增簽章後需明訂舊 ZIP 遷移方式；本輪不提議不可逆的晶片設定變更。

**7. 喚醒能力須有邊界。** Open GoPro 文件描述仍在 BLE 廣播期間可藉連線喚醒睡眠中的相機；不能推廣成任意關機後都能喚醒。[官方 BLE setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/)。我們的已存目標重連可能已有部分效果，應先測試，再決定需補哪些狀態／選項。DJI、Action 2 各自驗證，不套用 GoPro 行為。

### 建議交付順序

| 階段 | 範圍／交付成果 | 前置條件與驗收 |
| --- | --- | --- |
| 0：穩定現有版本 | 完成 1.1 UART passthrough／回滾與三協定回歸；補逐型號矩陣、FC/API 診斷 | 依 [現有驗證待辦](verification-status.md) 在 C3/C6 記錄結果；GoPro 先取得至少一個實測型號／韌體，不把軟體測試當硬體支援 |
| 1：首批功能 | 保存 USER 編號／OSD 二四行範本／C3 亮度；增加欄位有效性；先接 R SDK 既有遙測；低電量提示；Link Pause | 先透過既有 UART 管理器交付；每行 16 bytes、兩行模式相容、未知資料不顯示 0、暫停不丟配對 |
| 2A：錄影自動化 | 可選 ARM、非阻塞 stop delay；之後才加 momentary／Combi | 預設仍 USER；測試 ARM→disarm→倒數→rearm、手動操作、failsafe、FC/BLE 失聯、復原，不重複或延後誤送 STOP |
| 2B：手機設定 | 地面 AP／設定頁；再加 HTTP 更新與共用 OTA 核心 | 與 2A 可分開交付；兩板測記憶體／AP退出／恢復；中斷上傳、斷電、回滾、錯板套件均不能報假成功 |
| 3：機型功能 | GoPro 額外遙測／溫度和 SD 警告、本地 Action 2 timer、GoPro 預設／wake | 逐型號核對資料與缺欄位；預設切換等待相機確認，只在 idle；本地 timer 不冒充相機回報 |
| 4：範圍擴展 | 自動跨品牌辨識、Sony／Blackmagic、KISS Ultra；按需求評估 OSD 自動位置／簽章發佈 | 每項獨立規格與硬體；自動辨識只能根據廣播／GATT，不能用多種配對握手盲試；簽章若要公開發佈，可提前成獨立里程碑 |

**首批建議範圍：四行 OSD、可保存設定、低電量提示與 Link Pause。** 這一批直接改善飛行時資訊與日常使用，沿用 UART 入口，不需先增加另一套無線管理堆疊。Wi-Fi 是下一個操作體驗里程碑；ARM 是另一個可獨立交付的選項。

### 最小回歸清單

- **OSD／遙測：** 二／四行、剛好及超過 16 bytes、資料過期、真正零值、機型缺欄位、切換協定與關閉行；R SDK 單位比對，GoPro 可選查詢被拒仍保留基本狀態。
- **控制：** 按鈕和 USER 並行、重複邊緣、啟動時開關高位、FC／RX／BLE 失聯及恢復、saving、pending command；有 ARM 才新增 ARM／delay／Combi 測例。
- **Link Pause：** 停止掃描／重連、不 STOP、不刪 pairing、清空擁有的 OSD 行；錄影拒絕、離線可進入、恢復後等 inactive。
- **設定：** 斷電／NVS commit 失敗、舊設定遷移、無效值、重啟保存、管理 session 互斥；新增 USER 編號必須重建映射與基準狀態。
- **Wi-Fi／更新：** 實機記錄兩板 app size／最低 heap／stack、AP 不在飛行模式開啟、退出重連；UART＋HTTP 更新互斥、錯板／毀損／中斷／首次啟動回滾和版本／映像識別確認。
- **硬體支援標示：** 記錄相機型號／韌體、FC／BF版本與板型；既有 Action 2／Action 4 歷史實測與 GoPro 軟體實作狀態分開，沿用 [驗證文件](verification-status.md) 作發佈依據。

2026-10-01 的研究工作只完成官方資料與程式靜態對照及文件整理，沒有燒錄、實測 CamLink 或新增韌體功能。後續 2026-10-02 的首批實作、軟體測試與編譯結果另記於 [驗證紀錄](verification-status.md)；新功能尚待硬體驗收。
