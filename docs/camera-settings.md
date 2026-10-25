# RotorREC 1.2 設定、四行 OSD 與 Link Pause

版本：`1.2.0-dev`；更新日期：2026-10-25。錄影失敗提示、ARM／停錄延遲、遙測警告及地面設定便利性已有實作，**已完成本次軟體驗收，新功能未完成硬體驗收**。目前證據與待辦見[補完進度](recording-completion-plan.md)。跨品牌自動辨識與新相機品牌不在本次範圍。

## 可保存設定

沿用 [UART 管理器](passthrough.md)，每次保存後重啟 ESP，再讀回驗證實際生效值；不清除相機配對。GUI 必須先按 **Read status**，連線或 UART 改變後需重新讀取。操作期間會鎖定連線與設定欄位。

| 設定 | 範圍 | 未保存時的預設 |
| --- | --- | --- |
| USER | USER1–USER4 | 原 `menuconfig` 設定，通常 USER1 |
| OSD | 2 或 4 行 | 2 行 |
| C3 LED 亮度 | 1–100% | 5%，保持原亮度；C6 保存同一設定但 LCD 不受影響 |
| 相機低電量門檻 | 0–100% | 0＝關閉 |
| Link Pause | 開／關 | 關 |
| 錄影模式 | USER／ARM／Button／Combi／Off | USER |
| 上鎖停錄延遲 | 0–30 秒；ARM／Combi 生效 | 5 秒 |
| 自動開始／停止 | 各自開／關；ARM／Combi 生效 | 皆開 |
| 剩餘錄影時間警告 | 0–3600 秒；0＝關閉 | 0 |
| Heads-up | 一般溫度提醒開／關；重要錯誤不受影響 | 開 |

新錄影設定與舊基本設定合併保存於版本化 `settings_v2`；缺少此 key 時才從舊 `settings_v1` 遷移。四位元組舊管理請求保留新欄位，八位元組請求更新兩部分；損壞或未知版本不默默重設。INFO 同時提供目前及已保存的兩組值。

USER 使用持續開關位置；Button 用 USER 的瞬時按鍵切換；Combi 加上 ARM 自動化；Off 只停止自動控制，仍可操作相機本身或 BOOT。先給 USER 一次 LOW 基準，避免上電高位誤觸發。ARM 模式讀取真實飛控解鎖狀態。重新 ARM 取消停錄倒數；手動停止立即送出要求。失聯／未知狀態不視為停錄成功。待停止要求保存於 NVS，重新啟動後恢復；新的有效命令可取代，保存失敗顯示錯誤。

CLI 的全域參數放在命令之前；省略的設定會保留裝置已保存的值：

```sh
python tools/rotorrec_manager.py --port COM7 --uart 2 info
python tools/rotorrec_manager.py --port COM7 --direct settings --user 2 --osd-lines 4 --led-percent 25 --low-battery-percent 20
python tools/rotorrec_manager.py --port COM7 --direct link-pause on
python tools/rotorrec_manager.py --port COM7 --direct link-pause off
python tools/rotorrec_manager.py --port COM7 --direct settings --record-mode ARM --stop-delay-seconds 5
python tools/rotorrec_manager.py --port COM7 --direct reset pairings --confirm
python tools/rotorrec_manager.py --port COM7 --direct reset factory --confirm
```

`COM7`、UART 2 僅為範例；`--direct` 表示 FC 已在 passthrough。結束設定後須將 **FC 重新上電**，才能恢復 MSP／OSD／USER 控制；只重啟 ESP 不會退出 FC passthrough。

保存、重啟及更新會拒絕錄影、saving、pending command、仍連線但錄影狀態不明，以及更新正在進行的情況。尚未解決的錄影未知／未停止結果也會阻擋，即使 BLE 已離線。管理器不會替使用者送 STOP；維護前請讓相機保持待機。

設定採單一版本化 NVS key；缺少 key 的舊安裝使用上述預設。格式毀損或未支援版本會回報 storage error 並阻止 BLE 啟動，不會自動刪除配對。若保存完成但重啟未完成，INFO 會顯示等待重啟的設定；主機不會將讀回不一致當作成功。

## 四行顯示

在 Betaflight OSD 手動啟用並放置 Custom Messages 1–4；RotorREC 不自動更改 FC 版面。每行最多 16 bytes，僅輸出 ASCII。

| 行 | 範例 | 資料限制 |
| --- | --- | --- |
| 1 | `CAM REC 02:15` | 依相機確認的錄影狀態；有效時間或明示 `~` 的推算計時 |
| 2 | `B53% R01:30` | 電量＋有效剩餘 video 時間；缺欄位時保留 `CAM BAT 53%` |
| 3 | `4K16:9 60` | 有效的解析度／FPS；未知為 `CAM SETTINGS --` |
| 4 | `SD 128000MB` | 相機回報剩餘容量，單位 MB；未知為 `SD --` |

R SDK 資料在斷線、命令後或超過五秒未更新時失效。拍照／未知模式的時間不當作 video 秒數；慢動作 FPS multiplier 不冒充真實 FPS。真正的 0 秒或 0 MB 可顯示；sentinel 與未確認值顯示未知。Action 2 僅在觀察到確認 OFF→ON 後推算並標記 `~`，未知逾五秒或重連會清除。GoPro 解析可用的實際時間、容量、解析度／FPS；選配查詢不可用時退回基本狀態，不能推定所有機種都有這些欄位。

兩行預設保留原顯示。**一旦啟用四行，本模組持續擁有 3/4；改回兩行後會送空字串清除舊文字。** 從未啟用四行的安裝不寫 3/4。暫停也只清空模組擁有的行。若 ESP 失去電源或 UART 斷線，仍無法清除 FC 保留的舊文字，沿用 [OSD 限制](betaflight-setup.md)。

低電量仍可在第 2 行顯示 `LOW BAT 20%`；第一行依共用警告優先序顯示最重要問題。門檻 20% 在 ≤20% 觸發，升至 ≥23% 才解除；解除值最高為 100%。門檻 100% 對所有有效電量都提示。資料失效或關閉門檻時清除提示。

擴充警告會依優先序顯示於第一行，包括錄影失敗／未停止／未知、SD 缺失／滿／錯誤／速度不足、非影片模式、冷熱、低剩餘時間與低電量；同時存在的警告可於管理頁完整查看。不能用傳輸 ACK 代替相機錄影確認。剩餘時間門檻解除使用 10 秒遲滯。OSD 模板、Wi-Fi 設定／更新、清配對／出廠重設及 BF 位置工具詳見[設定器操作說明](../tools/web-configurator/README.md)。

## Link Pause

GUI **Pause camera link** 會保存暫停並重啟；暫停啟動時不初始化 BLE，因此不掃描、重連或 keepalive，也不執行 USER／短按錄影。保存的配對保持完整，換電後仍暫停。C3 藍燈常亮；C6 顯示 `LINK PAUSE`。

按 **Resume camera link** 或按住 BOOT 1.2–8 秒後放開，可保存解除暫停並重啟。BOOT 只送出請求，由 UART task 保存；保存失敗時保持暫停且不重啟。USER 恢復後等待 inactive 基準；ARM／Combi 依解鎖狀態及自動開始選項判斷。一般模式短按切換錄影，長按放開配對；至少八秒後放開則進入 Wi-Fi，兩種長按不會同時觸發。

此開關透過保存＋重啟生效，不提供即時 BLE pause API。回滾或降級到 1.1 會忽略 1.2 的設定，不能依賴舊韌體維持 Link Pause。

## 更新與實機驗收

1.0 單槽安裝仍需先透過 ESP USB 安裝完整雙槽 layout；已安裝 1.1 雙槽 layout 可使用對應板型的 1.2 update ZIP。[更新流程](passthrough.md) 不變。

在 C3、C6 各記錄相機型號／韌體、FC 型號／Betaflight 版本與結果：

1. 先完成原 1.1 passthrough／更新／回滾驗收，再驗證 1.2；Action 2／Action 4／GoPro 支援狀態分別記錄。
2. 缺少新 key 的安裝保持兩行與原 USER；保存全部設定、重啟及完全斷電後讀回。比對 C3 的 5／25／100% 實際亮度。
3. 二→四→二行，確認行長、3/4 清除及第一行的錄影錯誤／警告優先序；重要警告可覆寫一般錄影文字。對照相機畫面的時間、解析度／FPS、容量與單位；切換拍照、慢動作、未知資料及斷線不得顯示舊有效值。
4. 警告門檻上下反覆變動，確認遲滯、0% 有效電量及斷線清除。電量門檻測試須與相機回報一致，不用主機模擬代替實測。
5. 待機進入 Pause：BLE 中斷、無重連、OSD 清除、指示正確；斷電後仍暫停。管理器與 BOOT 分別恢復，配對保留，USER 高位不自動開始。錄影／saving／連線狀態不明或未解決的錄影未知／未停止結果必須拒絕；沒有這些狀態的完全離線才可進入。
6. 保存與更新期間斷電、重試與錯板套件，驗證 NVS／配對保留、無假成功與 OTA 回滾。量測實際 free heap、最大可用區塊及 task stack；編譯大小不代表運行時記憶體充足。

軟體測試、審查修正與編譯結果見 [verification status](verification-status.md)。完成上述驗收後才能將此開發版列為硬體驗證完成。
