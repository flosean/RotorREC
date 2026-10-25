# RotorREC 與 CAMLINK 功能對照

查核日期：2026-10-22。RotorREC 基準為 `885c6a1`、`1.2.0-dev`，包含目前的 Chrome／Edge Web Serial configurator。本文是功能與流程查核，不代表已完成本版硬體驗收，也不是新增功能的實作承諾。

後續已依使用者要求開始補完錄影控制、遙測及設定流程。本文保留當時基準比較；工作中功能與尚未完成項目請見[補完進度](recording-completion-plan.md)，不能將下表的舊差距直接當成修改後狀態。

## 2026-10-25 目前差距（優先閱讀）

本次重新讀取官方[商品頁](https://itsfpv.de/en-eu/products/camlink)與[設定指南／發布紀錄](https://itsfpv.de/en-de/pages/camlink-quickstart)，並核對目前未提交的原始碼與管理介面。官方仍列穩定發布 `1.0.0`、最新測試版 `1.1.0.b2`（2026-10-19）。本次是比較查核，未重新執行整套測試；既有軟體驗證及未完成項目見[補完進度](recording-completion-plan.md)。沒有操作硬體。

**已不能列為「沒有」：**錄影開始／停止失敗與未知提示、ARM／Button／Combi／Off、0–30 秒停錄延遲、待停止工作保存、Action 2 推算計時、GoPro 額外遙測與能力限定警告、OSD 文字模板、診斷匯出、BF OSD 放置工具、手機 Wi-Fi 設定／更新、清除配對與出廠重設。這些已有程式和操作入口；本次軟體驗收已完成，證據見補完進度；本版實機驗收仍另列待辦。

| 現在仍有的差距 | RotorREC 目前狀態 |
| --- | --- |
| 直接 AUX 學習、Low／Mid／High 區間與功能衝突提示 | Button／Combi 仍透過 BF USER 指派，沒有直接 AUX 學習頁面。 |
| 配對時跨品牌自動辨識 | 仍先選 Action 2／DJI R SDK／GoPro 協定並重啟。 |
| 更多相機 | 尚無 Nano、Sony、Blackmagic、Insta360 adapter；共用 R SDK 不等於各 DJI 型號已實機通過。 |
| GoPro 進階控制 | 尚無 preset 切換、HiLight、開機喚醒、GPS 時間同步、蜂鳴尋機；目前 preset group 是唯讀狀態辨識。 |
| Camera 2 | 只有單一當前相機，沒有同時控制第二台。 |
| 更完整的 OSD 編輯 | 已有七種 token 的四行文字模板；沒有拖曳編輯與完整相機能力提示。 |
| 手機整合 BF 設定 | 已有手機維護，但 Wi-Fi 模式停用 FC／BLE；BF OSD 放置需獨立桌面 USB 頁面，UART／USER 仍到 Betaflight Configurator 設定。 |
| KISS Ultra、自動 RX／TX 方向 | 尚未實作；目前限 Betaflight 與板型固定腳位。 |
| 發行者簽章 | 更新已有完整性、板型與映像檢查，尚無發行者簽章。 |

Camera 2、GO Ultra、GoPro 蜂鳴及自動 RX／TX 是 CAMLINK `1.1.0.b2` 測試版新增項，不能當成其穩定版既有承諾。以上 CAMLINK 功能取自官方指南；廠商列出的機型驗證不代表本專案已驗證。

從首次使用角度，仍有三個明確差異：RotorREC 開發板需要 ESP USB 首刷；C3 的 Wi-Fi 密碼先由有線工具／USB 日誌取得；經 FC passthrough 維護後，必須重新上電 FC 才恢復正常控制。這些是目前操作流程的限制，並非整個刷寫／手機設定功能不存在。

既有四項範圍的 HTTP 伺服端／BOOT 邊界測試與整體文件審核已補完。後續可改善 AUX 學習和設定流程；GoPro 進階控制與新品牌依實際相機需求排列。下方保留歷史基準，不能用舊缺口表取代本節。

## 2026-10-23 工作目錄補充（歷史）

本節以 `885c6a1` 加目前未提交修改為準，僅確認原始碼與介面是否存在；本次比較沒有重新執行整套測試或硬體驗收。以下歷史表格仍描述修改前的基準。

| 目前分類 | 項目與剩餘差距 |
| --- | --- |
| 已有新增程式，待最終整合驗收 | 共用錄影開始／停止失敗與未知提示、ARM／Button／Combi／Off、0–30 秒停錄延遲、重新 ARM 取消、待停止要求保存。不能再列為完全未實作。 |
| 已有新增程式，能力依相機而定 | Action 2 推算計時、GoPro 時間／容量／格式與儲存／溫度警告、DJI 溫度欄位、低剩餘時間門檻。協定解析與真機支援仍是兩種證據。 |
| 部分完成 | OSD 文字模板、診斷匯出、首次使用提示與獨立 Betaflight OSD 配置工具已有程式；尚未達到 CAMLINK 的整合式手機設定、拖曳與能力提示體驗。 |
| 部分完成 | Wi-Fi AP、HTTP 管理與網頁入口已有程式；尚缺最終整合、手機流程與更新復原驗收。配對／出廠重設已有韌體端工作，但管理介面尚未完整接通。 |
| 尚未實作 | 直接 AUX 學習與區間／功能衝突提示、跨品牌自動辨識、GoPro preset／HiLight／喚醒／GPS 時間／尋機、第二台相機、Nano／Sony／Blackmagic／Insta360 adapter、KISS Ultra、自動 RX/TX 方向、更新包發行者簽章。 |

當日可讀的[官方指南索引](https://itsfpv.de/en-de/pages/camlink-quickstart)仍列 `1.1.0.b2` beta（2026-10-19），包含 Camera 2、GO Ultra、自動 RX/TX 與 GoPro 蜂鳴；穩定發布項為 `1.0.0`。en-int 指南直接請求遇網站連線驗證，本次改以官方其他地區頁及搜尋索引交叉核對，未將 beta 功能當成穩定版承諾。

近期優先工作應是完成既有四項補完範圍的軟體驗收與文件，尤其 Wi-Fi 進出、重設介面、故障復原及 C3/C6 最終建置。新增品牌與進階 GoPro 功能則屬額外擴充。

## 修改前基準結論（2026-10-22）

RotorREC 已具備單相機對頻、保存配對、重連、USER 開關錄影、真實相機狀態、OSD、Link Pause、瀏覽器設定及雙槽更新。主要差距是 **ARM／多種錄影觸發與停止延遲、完整錄影風險警告、手機 Wi-Fi 設定、OSD 自動配置與版面編輯，以及更多相機／GoPro進階控制**。CAMLINK 最新 beta 另外加入雙相機與 Insta360 GO Ultra；不能把這兩項當成其穩定版既有功能。

「已有瀏覽器設定」必須保留：RotorREC 現在使用桌面 Chrome／Edge 透過 FC USB 的 Web Serial；缺的是模組本身提供的 Wi-Fi AP、手機免線材設定，以及免重新啟動 FC 的維護流程。[本專案 configurator](../tools/web-configurator/README.md)

## 比較版本與證據範圍

| 項目 | 本次依據 |
| --- | --- |
| CAMLINK 穩定版 | 官方發布區列 `1.0.0`，2026-09-01 發布 |
| CAMLINK 最新測試版 | `1.1.0.b2` beta，2026-10-19；指南頁首也是此版／日期 |
| CAMLINK 相容性資料 | 當次下載的指南內 `cl-compat-data`：`1.1.0.b2 / 6a20cc61`；個別機種區分廠商已測與預期可用 |
| RotorREC 軟體證據 | 原始碼、README、設定與 Web Serial 工具；既有軟體測試紀錄另見驗證文件 |
| RotorREC 實機證據 | 保留專案擁有者已確認的 Action 2、較早版 Action 4、Betaflight UART／OSD 與 LCD 結果；近期模組化、GoPro、設定／更新功能仍需本版實機回歸 |
| 本次限制 | 使用者目前未連接測試硬體；未操作序列埠、飛控或相機。CAMLINK 的「已驗證」均指廠商宣稱，並非本專案實測 |

官方來源：[商品頁（當日可讀的 en-eu 版本）](https://itsfpv.de/en-eu/products/camlink)、[最新設定指南與發布紀錄](https://itsfpv.de/en-int/pages/camlink-quickstart)。本機證據：[README](../README.md)、[驗證狀態](verification-status.md)。

本次直接 HTTP 取得最新 en-int 指南；搜尋服務部分地區頁快取仍顯示 1.0.0／2026-09-15，不能以舊快取否定新功能。[舊比較筆記](camlink-evaluation.md)保留歷史用途，不作為當前官網證據。

## 1.1.0.b2 beta 新增與變更

以下來自官方2026-10-19發布紀錄，應與1.0.0穩定版分開看：

- Camera 2：同一模組控制第二台相機，提供Shared／Independent模式與回到範圍後重連。
- Insta360 GO Ultra：新增支援，廠商稱已實機驗證。
- DJI Action 4、Action 5 Pro、Osmo 360／360 II：新增廠商實機驗證結果；不應沿用舊表的「只有預期支援」。
- GoPro Beep with the drone：在符合條件時跟隨飛控蜂鳴／失聯提示。
- RX／TX自動方向探測；MSP現在固定115200。
- 改善狀態字、警告呈現與預設值；重要錯誤維持開啟，heads-up集中管理。

來源：[官方Firmware updates與Updating from 1.0.0](https://itsfpv.de/en-int/pages/camlink-quickstart)。這些是廠商測試版的發布內容，不代表RotorREC已實作，亦不代表所有CAMLINK零售模組已更新。

## 已有與部分完成

| 功能／流程 | CAMLINK 官方現況 | RotorREC 現況與差距 | 判定 |
| --- | --- | --- | --- |
| BLE 對頻、保存與自動重連 | 支援已列機種，切換相機時重新對頻 | Action 2、DJI R SDK、GoPro 有獨立 adapter；GoPro 尚待實機驗收 | 已有基礎 |
| 遙控器開關錄影 | Arm、Switch、Button、Combi、Off | 透過 BF USER1–4 的持續開關位置；不是直接讀取任意 AUX，也不是 ARM 狀態 | 部分 |
| 錄影狀態確認 | 顯示相機確認的狀態與開始／停止失敗 | 已有未知狀態、pending、saving、防止重複 shutter 與相機確認流程；不能把 ACK 當錄影成功 | 已有基礎 |
| 遙控失聯處理 | Switch／Button／Combi 不跟隨失聯 AUX 變化；preset 有不同策略 | 已有 USER／FAILSAFE 狀態處理；缺其他控制模式，不能宣稱全部等效 | 部分 |
| OSD 狀態、電量 | 依相機能力呈現 | 已有；無效／過期資料不冒充即時值 | 已有 |
| 時間、容量、解析度、FPS | 依個別機種回報能力 | DJI R SDK 四行模式有經有效性判斷的欄位；Action 2／GoPro額外欄位仍不可用 | 部分 |
| 低電量警告 | heads-up 門檻與其他警告共同管理 | 已有可調低電量警告，預設關閉；其餘警告集合未完成 | 部分 |
| Link Pause | 保存配對、停止搜尋、隱藏自有 OSD，跨電池保存 | 已有持久化、BLE 停用及清除自有 OSD；可從管理工具恢復或長按 BOOT 恢復 | 已有 |
| LED 亮度與狀態 | 狀態燈、三檔亮度 | C3 有狀態燈與 1–100% 亮度；C6 另有 LCD | 已有 |
| 瀏覽器設定 | 模組 Wi-Fi AP 的手機／電腦網頁 | 已有桌面 Web Serial，含相機、配對、設定與 ZIP 更新；沒有 Wi-Fi AP | 部分 |
| 韌體更新／回滾 | Wi-Fi 上傳簽署 `.camfw`，拒絕錯裝置與 raw bin，開機失敗回滾 | UART 更新 ZIP、CRC／SHA-256、板型／容量／app檢查、版本／ELF指紋讀回、雙槽回滾已實作；無發行者簽章、無 Wi-Fi 更新 | 部分 |
| 首次安裝與刷寫 | 預裝韌體的零售硬體；零售板不接受線材刷寫 | 開發板首次安裝、舊單槽遷移、bootloader／分區救援仍需 ESP USB；Web Serial 不提供 ESP ROM 首刷 | 產品形式不同 |

官方行為來源：[設定指南的 Recording、OSD、Setup Mode、Firmware updates](https://itsfpv.de/en-int/pages/camlink-quickstart)。本機依據：[控制策略](../main/betaflight/bf_control.c)、[相機控制器](../main/camera/camera_controller.c)、[OSD](../main/betaflight/osd_messages.c)、[保存設定](../main/management/settings.h)、[更新協定](../main/management/management.c)、[管理工具說明](passthrough.md)。

## 尚未具備的主要功能

| 差距 | CAMLINK 提供的行為 | RotorREC 需要補上的範圍 |
| --- | --- | --- |
| ARM 自動錄影與延遲停止 | 解鎖開始；上鎖後 0–30 秒延遲停止，預設建議 5 秒；期限內重新解鎖保持同一段錄影。開始與停止自動化可獨立開關 | ARM 狀態、延遲狀態機、重新解鎖取消、遲連線／重連與手動停止的優先序 |
| 重啟後的待停止工作 | 保留待停止要求及剩餘延遲，重啟後恢復；新控制命令可取代 | 沒有持久化錄影控制意圖；也缺與CAMLINK對等的明確OSD開始失敗／未停止／錄影未知訊息 |
| Button、Combi、監看模式 | AUX 瞬時按鍵切換；Combi 結合 ARM 與手動起停；Off 只回報狀態 | 原始 AUX 讀取、按鍵去彈跳、控制模式設定、明確的手動 override；不能以 BOOT 實體按鍵當成遙控器 Button 模式 |
| AUX 學習與衝突提示 | 即時通道學習、Low／Mid／High 區段，提示 preset／HiLight／Camera 2 通道占用 | 現在需到 BF Modes 指派 USER；沒有此通道學習介面 |
| 手機無線維護 | 模組提供 Wi-Fi 設定網頁，保存／退出後回到運行 | AP／網頁服務與維護模式。現有 FC passthrough 維護完必須重啟 FC 才恢復 MSP／USER／OSD |
| 配對時自動辨識品牌 | 支援的相機直接進行對頻，不必另選品牌設定 | 現在需先選 Action 2／DJI R SDK／GoPro 協定並重啟；未有跨品牌自動探測 |
| 清除配對與恢復出廠設定 | 頁面可移除相機，Factory Reset 清除設定與配對；按鍵操作受 ARM／錄影狀態限制 | 現有重新配對會更換目標，但沒有完整的移除配對／恢復出廠介面；也沒有依 ARM 狀態限制維護按鍵的流程 |
| 一次性 OSD 安裝檢查 | Betaflight 自動啟用／放置四條 Custom Message，讀回檢查；Before You Fly 顯示待完成項目 | RotorREC 仍需使用者在 BF 配置 UART／Modes／OSD；缺此整合式檢查及自動放置 |
| 自訂 OSD 版面 | 四行、每行 16 字元與最多 8 tokens；拖曳、預設版面、相機能力提示 | 目前只有固定二／四行。最新指南列 20 個主相機 token，另有 Camera 2 狀態 token；不是所有相機都支援全部欄位 |
| Action 2 錄影計時與 GoPro 額外遙測 | Action 2 由確認開始後本機計時，斷線超過一分鐘清除；GoPro 依機型提供更多錄影／儲存／格式資訊 | Action 2 尚無這種推算計時；GoPro 目前主要解析錄影、忙碌、就緒、電量與影片模式，尚缺時間、剩餘空間及拍攝格式欄位 |
| 完整錄影風險警告 | 無卡、卡滿／卡錯誤、非影片模式、過熱、過冷、剩餘容量時間、錄影失敗／未知／意外停止／未停止等，能力依相機而定 | shared camera state 的資料與每個 adapter 的能力映射、優先序／重複提示、OSD與LED；現有低電量與連線／pending狀態不能等同整套警告 |
| GoPro preset／HiLight | AUX 選預設、最多四台相機的獨立映射、錄影中延後套用或停止後重開，標記精彩片段 | 目前僅讀取 preset group 以辨識影片模式；沒有列出／切換 preset、HiLight 或映射持久化 |
| GoPro喚醒、時間與尋機 | Auto wake；飛控GPS時間加時區並讀回確認；beta加入與飛控beeper／失聯連動的GoPro蜂鳴 | 目前的連線keepalive不是開機喚醒，也不是時間同步；這三項均未提供 |
| 多相機品牌／機種 | 官方相容表列 DJI Nano、GoPro、Sony、Blackmagic；beta另有GO Ultra與擴充DJI驗證 | 未有Nano、Sony、Blackmagic、Insta360 adapter；R SDK存在不能等同所有DJI機種已驗證 |
| Camera 2（beta） | 第二台獨立配對；Shared／Independent起停；離開範圍後保留其可能仍錄影的狀態，回範圍重連 | 本專案目前單一當前相機控制器，沒有同時兩台與跨相機停止確認 |
| KISS Ultra | B67+，MSP serial與手動Custom Text 1–4 | 目前明確針對Betaflight；沒有KISS辨識／配置／驗收 |
| 自動 TX／RX 方向與診斷匯出 | beta可自動探測方向；網頁呈現Connected／Silent／Degraded，提供支援診斷檔 | 目前固定板型腳位、有管理狀態／USB日誌；缺自動方向探索與一鍵支援診斷包 |
| 更新來源驗證 | 發行者簽署更新包 | SHA-256／CRC只能檢查完整性，不能取代可信發行者簽章 |

上述 CAMLINK 行為均取自[當前官方指南](https://itsfpv.de/en-int/pages/camlink-quickstart)，其中 Camera 2、GO Ultra、GoPro尋機蜂鳴、自動RX/TX為 1.1.0.b2 發布說明的新增項目。本機缺口核對：[相機種類與快照欄位](../main/camera/camera_types.h)、[GoPro協定](../main/camera/gopro/gopro_protocol.c)、[GoPro adapter](../main/camera/gopro/gopro.c)、[BF bridge](../main/betaflight/bf_bridge.c)、[Web configurator](../tools/web-configurator/README.md)、[相機協定研究](camera-protocol-research.md)。

## 相機支援不可混為一談

| 類別 | CAMLINK 最新指南的廠商說法 | RotorREC 證據 |
| --- | --- | --- |
| Action 2 | 列為廠商已驗證 | 擁有者已驗證基礎配對／錄影／狀態；本次協定整理仍需回歸 |
| Action 4 | beta發布紀錄新增實機確認；指南明示早期版本的第三方測試曾無法配對 | 較早實作已驗證連線／錄影／設定；當前模組化版本待回歸 |
| Action 5 Pro、Action 6、360／360 II | 最新相容資料均列廠商已驗證；5 Pro與360系列部分驗證為beta更新 | 只有共用R SDK協定路徑，不能列作本專案逐機種驗證成功 |
| Osmo Nano | 列為廠商已驗證，睡眠後需相機按鍵喚醒 | 尚無Nano adapter；不能直接套用Action 2／R SDK當成支援 |
| GoPro | 部分機種廠商已驗證、其餘預期可用；不能把品牌整體當相容清單 | Open GoPro adapter已寫，實機相容性仍待驗證 |
| Sony／Blackmagic | 有列明已驗證與預期機種；Blackmagic要求相機韌體8.1+ | 無adapter |
| Insta360 GO Ultra | 1.1.0.b2新增，廠商稱已驗證 | 無adapter；也不能延伸成全部Insta360皆支援 |

來源：[官方相容表與beta發布紀錄](https://itsfpv.de/en-int/pages/camlink-quickstart)、[本專案支援狀態](../README.md)、[協定證據與Nano限制](camera-protocol-research.md)。

## 官網版本差異與使用限制

- **KISS舊矛盾已消失**：當日商品頁與最新指南都明列Betaflight 2025.12+／KISS Ultra B67+。不能再沿用舊筆記的「商品頁說不支援、指南說支援」。自動OSD放置仍只限Betaflight，KISS需要手動放置。[商品頁](https://itsfpv.de/en-eu/products/camlink)、[指南](https://itsfpv.de/en-int/pages/camlink-quickstart)
- **不要混用1.0設定規格**：beta改成MSP固定115200、RX/TX可Auto；卡／模式／錄影問題持續提示，只有四項heads-up可共同靜音，預設開啟。不是所有警告都仍可逐項關閉。[升級說明](https://itsfpv.de/en-int/pages/camlink-quickstart)
- **雙相機不是雙份完整遙測**：Camera 2顯示狀態與警告，沒有第二份電量／容量數值；preset、HiLight、GPS時間與蜂鳴只在Camera 1。第二台在地面離線時可能仍錄影，必須回到範圍才送得出停止。[Camera 2說明](https://itsfpv.de/en-int/pages/camlink-quickstart)
- **GoPro時間同步不是影格同步**：TC代表時鐘寫入並讀回成功；沒有跨相機frame-accurate timecode保證。Preset載入失敗時，CAMLINK仍可能以舊preset開始新片段，實作不能假設設定已切換成功。[GoPro功能](https://itsfpv.de/en-int/pages/camlink-quickstart)
- **不能宣稱能喚醒DJI**：CAMLINK官方同樣說無法喚醒睡眠DJI；GoPro Auto wake不可推廣到所有相機。[相機睡眠說明](https://itsfpv.de/en-int/pages/camlink-quickstart)

## 建議補齊順序

1. **先讓現有軟體流程容易判斷是否成功**：整合首次使用、飛控通訊、配對、錄影確認與維護後恢復的檢查，明示故障原因。使用者目前沒有測試硬體，可先完成軟體設計與模擬驗證；兩款ESP、Action 2／4、GoPro、透傳返回正常控制、更新中斷與回滾另保留實機驗收清單。
2. **錄影完整性優先**：ARM模式＋停止延遲、明確開始／停止確認與SD／模式／溫度能力、飛控／相機失聯後不猜測狀態。先覆蓋每天飛行最可能漏錄或截斷檔案的流程。
3. **降低首次使用與現場維護成本**：在已有Web Serial基礎上加入整體檢查、BF設定讀回／OSD配置、可分享診斷；有手機無線需求再加入AP與Wi-Fi更新。
4. **相機優先順序依實際需求決定**：Nano獨立adapter與GoPro進階功能比一次加入所有品牌容易定義驗收。每一台記錄型號、camera firmware及可用欄位。
5. **再做進階擴充**：OSD自由排版、Camera 2、KISS、Sony／Blackmagic／GO Ultra；若要發布可廣泛散布的二進位，再納入更新簽章與金鑰管理。

此順序以RotorREC的當前單相機FPV用途與已存在工具為基礎，不要求為了達成功能數量而一次複製CAMLINK全部行為。
