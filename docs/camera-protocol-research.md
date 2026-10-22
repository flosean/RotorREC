# DJI／GoPro 控制協定與公開範本查核

查核日期：2026-10-22。使用者提到的 DJI Nano 在本文指 **DJI Osmo Nano**。使用者確認 **RotorREC 的 Action 2 已完成實機驗證**，本次保留其既有工作基線；本文新增研究與變更仍須另外回歸，不將舊有成果重標為未驗證。既有 [CAMLINK 功能評估](camlink-evaluation.md) 保留產品層比較；本文整理這次會影響協定實作的發現。

## 結論與來源分級

| 對象 | 可用依據 | 整合界線 |
|---|---|---|
| DJI Osmo Action 4／5 Pro／6、Osmo 360 | DJI 官方 ESP-IDF demo、R SDK 協定文件 | 官方列出的 R SDK 機型；仍須逐機型／韌體測試 RotorREC |
| DJI Action 2 | 使用者已實機驗證的 RotorREC DUML 整合、既有捕包／測試，ShutterLink 等社群程式 | 保留既有可用行為；不在上述官方 R SDK 支援清單，不能套用 R SDK 狀態欄位 |
| DJI Osmo Nano | Multicam、Osmosis、ShutterBridge 的 DUML 實作 | DJI 官方表目前仍標記未支援、等待未來 firmware；社群 BLE 支援不等於官方 R SDK 支援 |
| GoPro | Open GoPro 官方 BLE 規格、Python SDK／其他 demo | 依 API／能力與確切機型處理，不宣稱所有 GoPro 或 HERO8+ 通用 |
| itsFPV CAMLINK | 廠商使用指南與相容資料 | 功能行為參考；本次未找到廠商公開 firmware 原始碼，不能當可直接移植的 SDK |

DJI 官方 README 列 Action 4／5 Pro／6、360；協定資料表對 Nano 明列「Not supported yet」。Action 2 不在清單內。這與 CAMLINK 自行支援 Nano／Action 2 並不矛盾：可以採另一條 DUML 協定路徑。[DJI README](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo)、[DJI 支援表](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/docs/protocol_data_segment.md)、[CAMLINK 指南](https://itsfpv.de/en-de/pages/camlink-quickstart)

### Nano 的官方支援表會不會過時？

有可能，不能只憑舊表斷言所有最新 Nano firmware 都不支援。此次另查官方更新與 issue：

- 支援表最後內容更新為 **2025-11-17**，commit `92fe23e5` 是加入 Action 6，並不是 2026 年 Nano 新結論。[檔案歷史](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/commits/main/docs/protocol_data_segment.md)
- 官方 repo contributor `djizachy` 在 **2025-11-03** 回覆 Nano 尚不支援，等待未來 firmware；issue 雖 closed，並不表示功能完成。**2026-08-22** 社群作者 KonradIT 仍在同串提供自行完成的 Nano DUML 程式；他不是 DJI 官方發言者。[官方 repo issue #10](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/issues/10)
- 截至本次查核，DJI [Nano 下載中心](https://www.dji.com/downloads/products/nano) 仍連結 **2026-02-13、v01.01.27.51** 發布紀錄，列 9:16、Film Tone 與修正，沒有宣布 R SDK。該紀錄不能證明相機絕無較新 OTA 或未記錄的能力。[官方發布紀錄](https://dl.djicdn.com/downloads/Osmo_Nano/20260213/DJI_Osmo_Nano_Release_Notes_en.pdf)
- 官方 repo 此次讀取的全部 PR 僅有 logging/performance 變更，未找到 Nano 支援合併。[PR 清單](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/pulls?q=is%3Apr)

因此目前結論是 **尚未找到官方公開確認 Nano 支援 R SDK 的證據**。後續 Nano 整合應以已公開、可核對的 DUML 路徑作候選；本次沒有啟用 Nano adapter，也未將它掛在 Action 2 或 R SDK 選項下；若將來確定機身 firmware 對官方 `0xAA` 握手、狀態訂閱與錄影控制完整回應，再依實際能力選 R SDK。機型上市時間本身不足以決定協定。

## 優先看的實作範本

| 範本 | 適合借鏡 | 查核版本與授權 |
|---|---|---|
| [DJI Osmo-GPS-Controller-Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo) | ESP-IDF、FFF0/4/5、R SDK 連線、錄影、狀態訂閱 | `92fe23e5a749f189593f980a26a105c3bb66aa1c`；LICENSE 同時保留 DJI EULA 引用、demo MIT 文字，沿用本 repo 的第三方聲明 |
| [GoPro OpenGoPro](https://github.com/gopro/OpenGoPro) | 官方 BLE API、Python／C#／Kotlin 範本，封包與狀態型別 | `0f963572611c4410a15678531e9681a6ff874edb`；各 demo 有個別授權，不將整個 repo 泛稱 MIT |
| [Action Multicam Remote](https://github.com/dstrat28/action-multicam-remote) | Nano 專用 routing、keepalive、狀態與模式分流；README 作者列 Nano 實測、Action 2 不支援 | `b89cd0022e5a50b357a7fdbab154b9a5ff037e21`；MIT |
| [DJI-ESP32-Remote / side-by-side](https://github.com/KonradIT/DJI-ESP32-Remote/tree/side-by-side) | Nano ESP-IDF DUML 實作、實機控制結果、R SDK／DUML 分流 | `354dd83e2afb31e329bb41189ab8c93561eecafb`；MIT；該 branch 文件有過時命令，以原始碼與捕包註記核對 |
| [Osmosis](https://github.com/KonradIT/osmosis) | Nano 配對、DUML framing、session routing、原始捕包研究 | `e82fd931622404807f2e983c7df1ca51254c1bcd`；MIT；主要用途是 BLE 配對後 Wi-Fi 卸載媒體 |
| [ShutterBridge](https://github.com/YLabs-FPV/ShutterBridge) | 與本專案接近的 ESP32／Betaflight 架構，分開 DJI R SDK 與 Nano DUML adapter | `ef90756d8413f74bbc51ec5b8ff02b12437eaec6`；GPL-3.0，本文僅比較協定／設計，未複製其程式碼 |
| [ShutterLink](https://github.com/rover1312/shutterlink) | ESP32-C3、Action 系列 DUML、配對狀態機 | `5f969d097469584d32ac709a99a5e50b624c5933`；MIT；README 與實作有差異，需看原始碼 |

版本是查核當時 GitHub API 回傳的 main HEAD（DJI-ESP32-Remote 例外，取 side-by-side HEAD）。MIT 程式若日後複製仍須附原作者授權與版權文字。DJI 與 GoPro 的授權入口分別為 [DJI LICENSE](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/LICENSE)、[GoPro LICENSE](https://github.com/gopro/OpenGoPro/blob/main/LICENSE)；其他專案授權見各 repo 的 LICENSE／LICENSE.txt。

## DJI 官方 R SDK：可直接依規格修正的部分

R SDK 走 `0xAA` frame；Action 2／Nano 使用的 legacy DUML 走 `0x55`。即使同樣使用 FFF0 service、FFF4 notify、FFF5 write，也不能混用封包解析器。DJI FAQ 特別指出 Action 5 Pro 可同時推送 DUML 與 R SDK frame，R SDK client 應只處理 `0xAA`。[官方 FAQ](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/docs/Q%26A.md)

以下命令／單位依 [官方資料段規格](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/docs/protocol_data_segment.md)：

| 命令 | 定義 | RotorREC 實作重點 |
|---|---|---|
| `00/19` | 連線申請與相機主動驗證結果 | 首配 verify_mode=1，已配=0；必須等相機主動回報允許，再回覆，不能把立即 ACK 當配對完成 |
| `1D/03` | 明確錄影控制；record_ctrl=0 開始、1 停止 | 和 DUML `02/02` 的 1／0 方向相反；保留命令與實際狀態確認分離 |
| `1D/05` | 狀態訂閱；push_mode=3 為週期＋狀態變化 | push_freq=20，官方指定固定 2 Hz |
| `1D/02` | 相機狀態 | status=3 是拍照或錄影；5 是預錄，應另辨識；record_time 秒包含預錄，photo burst 時卻是毫秒 |
| `1D/06` | 新機型／模式文字與拍攝參數 | 超過舊模式範圍不能猜解析度與 FPS；驗證字串長度、型別 |
| `00/00` | 版本查詢 | 不是正式 heartbeat；response 起始是 2-byte ack_result，其後 product_id／版本字串 |

實作推論：只有明確可錄影模式、合法 status 與新鮮資料，才可提供有效錄影狀態；Photo、未知模式、未知 status 不應顯示成「已確認停止／錄影」。預錄不能直接等同檔案已正式開始錄製。剩餘時間為秒，容量為 MB，電量有效範圍 0–100。這些限制是本專案避免錯誤控制的政策，不是 DJI 承諾的硬體測試結果。

## Action 2 與 Nano：共用 DUML 核心、保留機型差異

可共用的是 framing／CRC、buffer 重組、ACK 比對與 app-level 配對；不可直接共用所有遙測欄位。Osmosis 記錄 `07/45` 的 `00 01` 是已配對、`00 02` 是等待相機批准；首次批准另由 `07/46` **request** 到達，需同 sequence 回覆。重試要保持相同識別碼；廣播名稱或 FFF0 只能作發現線索，不能證明協定已準備好。[Osmosis 協定圖](https://github.com/KonradIT/osmosis/blob/main/docs/01-protocol-map.md)

本專案 Action 2 的 `02/70` 與 `0D/02` 以現有捕包／測試為準；`02/80` 候選欄位不因另一個機型能解碼便自動升格為有效。ShutterLink README 曾列錄影 `0A/0D`，但其目前 [dji_camera.cpp](https://github.com/rover1312/shutterlink/blob/main/src/dji_camera.cpp) 明確改成 `02/02`、payload 1 開始／0 停止，並承認舊註解沒有依據。這是不能只照範本 README 整合的實際例子。

Nano 最有用的獨立對照為 [DJINanoProtocol.swift](https://github.com/dstrat28/action-multicam-remote/blob/main/ActionCamRemote/Bluetooth/DJINanoProtocol.swift) 與 [DJIExperimentalBLEClient.swift](https://github.com/dstrat28/action-multicam-remote/blob/main/ActionCamRemote/Bluetooth/DJIExperimentalBLEClient.swift)：

| Nano DUML 候選流程 | 社群實作內容 | 整合注意 |
|---|---|---|
| session open | `00/2B`、`04 00` → receiver `F0` | 與拍攝命令的 receiver `01` 不同 |
| keepalive | `00/2B`、`01 01` → `F0`，每秒 | 不移用 R SDK `00/00` 當 heartbeat |
| system wake | `53/10`、四個零 → `1C` | 需要使用者預期的喚醒政策；不能因重連而無限喚醒 |
| session info | `00/32`、`31 31 00 00 00` → `88` | 對方子系統不同，不可所有命令都發 `01` |
| record | `02/02`，1 開始／0 停止 → `01` | ACK 與錄影狀態分開確認 |
| authoritative mode | `02/80` 非 response；payload offset 57 | 短包缺該欄位就未知，不把 workMode@4 當完整拍攝模式 |

目前公開範本仍有衝突：[ShutterBridge DjiOsmoCamera.cpp](https://github.com/YLabs-FPV/ShutterBridge/blob/main/firmware/src/camera/dji/DjiOsmoCamera.cpp) 把 elapsed@29 讀為 LE32，而 Multicam 讀 LE16；前者的初始化 routing 亦不等同後者。Osmosis 的 BLE idle 斷線說明是媒體卸載 session，不能直接當長時間 BLE 遙控已驗證。故 Nano 必須保留為獨立且明確標示的實驗性整合，待本專案捕包確認 elapsed 長度、錄影 bits、saving、模式、電池與 dock 差異；不能將 Action 2 adapter 改名便宣稱完成 Nano 支援。

更貼近本專案的 [DJI-ESP32-Remote command_logic.c](https://github.com/KonradIT/DJI-ESP32-Remote/blob/side-by-side/logic/command_logic.c) 記錄其 Nano 對 `02/02` 的實測：start ACK 後約 860 ms 才看到錄影 bit，stop 約 2.4 秒才清除；這些是作者當次量測，不能設成所有機型保證時間。同 branch 文件仍寫 `02/20`／`02/21`，但程式註解已指出該機身回覆 `e0` 不支援。採 `02/02` 與狀態確認有交叉來源支持，勿執行無依據 opcode 掃描或將文件舊命令一併送出。

## GoPro：先用官方命令與狀態，分開必要／選用查詢

官方 [BLE setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/) 要求先完成 BLE 配對、每次連線重新訂閱 notify，並輪詢 hardware info 直到成功。應保留 response stream 各自重組和 command transaction 邊界，寫入成功不是錄影成功。官方 [control](https://gopro.github.io/OpenGoPro/docs/ble/control/) 建議 keepalive 每 3 秒，固定值 `0x42`。

官方 Python [gopro_wireless.py](https://github.com/gopro/OpenGoPro/blob/main/demos/python/sdk_wireless_camera_control/open_gopro/gopro_wireless.py) 現況值得分清：

- 仍在 BLE session 開啟時送 `set_pairing_complete()`，也檢查 API 版本匹配；沒有足夠證據直接刪除本專案對應要求。
- `set_third_party_client_info()` 對部分機型可能沒有回應，官方捕捉該命令的 timeout 後繼續。可讓這項分析／識別資訊的 timeout 不致阻斷錄影連線；其他必要交易仍需照常處理失敗。

可補入 shared snapshot 的正式狀態依 [ble_statuses.py](https://github.com/gopro/OpenGoPro/blob/main/demos/python/sdk_wireless_camera_control/open_gopro/api/ble_statuses.py) 型別與單位：

| Status ID | 內容 | BLE 型別／單位 |
|---|---|---|
| 13 | 已錄影時間 | unsigned BE32，秒；未錄影為零 |
| 35 | 剩餘錄影時間 | unsigned BE32，秒 |
| 54 | 剩餘卡容量 | unsigned BE64，KB；不能用四位元組解析 |
| 60 | 建議最小狀態查詢間隔 | unsigned BE32，毫秒 |
| 8／10／70／82／96 | busy／encoding／battery／ready／preset group | 維持目前基本控制狀態查詢 |

狀態註冊 `0x53` 初次回覆含目前值，後續更新走 `0x93`；可作未來減少 polling 的依據。[官方 query](https://gopro.github.io/OpenGoPro/docs/ble/query/) 本次最小整合策略是保留已存在的基本輪詢，選用遙測另查、各欄位有有效性；不支援選用欄位不得讓基本錄影狀態失效。容量為零不能自行推定未插卡，應另外查明確錯誤狀態。[官方狀態清單](https://gopro.github.io/OpenGoPro/docs/ble/statuses/)

## 本次已整理的程式與尚未啟用的項目

- **Action 2**：保留已驗證的配對、`02/02` 起停、`02/70` 錄影／saving、`0D/02` 電池流程與 payload。將 framing／CRC 移到 `main/camera/dji/dji_duml.c`，處理 BLE 分片、連續多包與損壞封包重新同步；獨立捕包測試確認送出的 bytes 不變。此核心不是 Nano 支援宣告。
- **DJI R SDK**：已配對握手使用 verify_mode=0；修正無 ACK 時重複等待同一批准訊息；驗證版本回覆成功。Photo／預錄／playback／未知 mode/status 不授權快門，電池等獨立遙測可繼續顯示。錄影命令成功 ACK 後，仍保持 pending，等新回報確認目標狀態；逾時不重送並重建 session。BOOT 和 USER 共用非阻塞請求。斷線 callback 不再阻塞等待 BLE，應用程式的重連工作重新握手與訂閱；拒絕／握手失敗後保留手動重新配對流程。
- **GoPro**：集中官方命令／狀態 ID，驗證 API 2.0；依官方 SDK 容許第三方識別沒有回覆，但不吞掉 GATT 失敗、部分回覆、錯誤回覆或斷線。其他交易仍須完整回覆；保留 Video 群組與實際 encoding 判斷。匹配回覆一旦收到，不再被後續通知覆蓋。
- **Nano**：完成範本、候選命令與衝突清單，尚無可選取的 Nano adapter。需要確切機身 firmware 和實機封包確認才能把候選欄位加入正式控制流程。
- **未新增的功能**：自動切換相機模式、Nano 喚醒／額外初始化、GoPro 額外時間／容量遙測、電源控制。上表中的官方命令／候選欄位是整合依據，不表示全部已實作。

## 本專案驗收門檻

本次軟體驗證涵蓋 DUML 分段／連續多包／壞 CRC／邊界、配對 ACK 與批准順序、錄影 ACK 後狀態確認與逾時、非法 mode/status、舊資料與斷線失效，以及 GoPro API 版本／封包／基本狀態。GoPro client-identification timeout 分支與應用層完整重連工作目前僅經程式檢查及兩板編譯，仍須實機驗證。未來啟用選用遙測時須另驗證未知 status 與 BE64 容量轉換；本次未宣稱已覆蓋。

每種相機另記錄機型、camera firmware、C3/C6 板型，測試首次批准、已配重連、相機／模組重啟、手動錄影時連線、20 次開始停止與檔案、錄影中斷線、Photo／預錄／saving、電池與容量對照，以及長時間 idle。Nano 加測 camera body／dock 分離及 keepalive；Action 2 保留其既有捕包基準。相機未知／過期狀態不得觸發猜測性的 STOP；失敗後不重送會造成切換的 shutter。
