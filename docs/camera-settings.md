# RotorREC 1.2 設定、四行 OSD 與 Link Pause

版本：`1.2.0-dev`；更新日期：2026-10-02。已完成程式、主機回歸與 C3/C6 編譯，**新功能尚未燒錄或完成硬體驗收**。本批依 [CamLink 評估](camlink-evaluation.md) 實作第一階段；Wi-Fi、ARM／延遲停錄、跨品牌自動辨識及新相機品牌仍是後續提案。

## 可保存設定

沿用 [UART 管理器](passthrough.md)，每次保存後重啟 ESP，再讀回驗證實際生效值；不清除相機配對。GUI 必須先按 **Read status**，連線或 UART 改變後需重新讀取。操作期間會鎖定連線與設定欄位。

| 設定 | 範圍 | 未保存時的預設 |
| --- | --- | --- |
| USER | USER1–USER4 | 原 `menuconfig` 設定，通常 USER1 |
| OSD | 2 或 4 行 | 2 行 |
| C3 LED 亮度 | 1–100% | 5%，保持原亮度；C6 保存同一設定但 LCD 不受影響 |
| 相機低電量門檻 | 0–100% | 0＝關閉 |
| Link Pause | 開／關 | 關 |

CLI 的全域參數放在命令之前；省略的設定會保留裝置已保存的值：

```sh
python tools/rotorrec_manager.py --port COM7 --uart 2 info
python tools/rotorrec_manager.py --port COM7 --direct settings --user 2 --osd-lines 4 --led-percent 25 --low-battery-percent 20
python tools/rotorrec_manager.py --port COM7 --direct link-pause on
python tools/rotorrec_manager.py --port COM7 --direct link-pause off
```

`COM7`、UART 2 僅為範例；`--direct` 表示 FC 已在 passthrough。結束設定後須將 **FC 重新上電**，才能恢復 MSP／OSD／USER 控制；只重啟 ESP 不會退出 FC passthrough。

保存、重啟及更新會拒絕錄影、saving、pending command、仍連線但錄影狀態不明，以及更新正在進行的情況。相機完全離線時允許恢復操作，不讓斷線前留下的錄影旗標阻擋維護。管理器不會替使用者送 STOP；維護前請讓相機保持待機。

設定採單一版本化 NVS key；缺少 key 的舊安裝使用上述預設。格式毀損或未支援版本會回報 storage error 並阻止 BLE 啟動，不會自動刪除配對。若保存完成但重啟未完成，INFO 會顯示等待重啟的設定；主機不會將讀回不一致當作成功。

## 四行顯示

在 Betaflight OSD 手動啟用並放置 Custom Messages 1–4；RotorREC 不自動更改 FC 版面。每行最多 16 bytes，僅輸出 ASCII。

| 行 | 範例 | 資料限制 |
| --- | --- | --- |
| 1 | `CAM REC 02:15` | 依相機確認的錄影狀態；時間僅使用有效 R SDK video 欄位 |
| 2 | `B53% R01:30` | 電量＋有效剩餘 video 時間；缺欄位時保留 `CAM BAT 53%` |
| 3 | `4K16:9 60` | 有效的解析度／FPS；未知為 `CAM SETTINGS --` |
| 4 | `SD 128000MB` | 相機回報剩餘容量，單位 MB；未知為 `SD --` |

R SDK 資料在斷線、命令後或超過五秒未更新時失效。拍照／未知模式的時間不當作 video 秒數；慢動作 FPS multiplier 不冒充真實 FPS。真正的 0 秒或 0 MB 可顯示；sentinel 與未確認值顯示未知。Action 2、GoPro 本批保留基本狀態／電量，額外欄位顯示未知，不增加推估計時器或相容機型承諾。

兩行預設保留原顯示。**一旦啟用四行，本模組持續擁有 3/4；改回兩行後會送空字串清除舊文字。** 從未啟用四行的安裝不寫 3/4。暫停也只清空模組擁有的行。若 ESP 失去電源或 UART 斷線，仍無法清除 FC 保留的舊文字，沿用 [OSD 限制](betaflight-setup.md)。

低電量提示使用第 2 行，例如 `LOW BAT 20%`，第 1 行繼續顯示錄影狀態。門檻 20% 在 ≤20% 觸發，升至 ≥23% 才解除；解除值最高為 100%。門檻 100% 對所有有效電量都提示。資料失效或關閉門檻時清除提示。

## Link Pause

GUI **Pause camera link** 會保存暫停並重啟；暫停啟動時不初始化 BLE，因此不掃描、重連或 keepalive，也不執行 USER／短按錄影。保存的配對保持完整，換電後仍暫停。C3 藍燈常亮；C6 顯示 `LINK PAUSE`。

按 **Resume camera link** 或按住 BOOT 1.2 秒，可保存解除暫停並重啟。BOOT 只送出請求，由 UART task 保存；保存失敗時保持暫停且不重啟。恢復後重新連線並等待 USER inactive 基準，開關原本在高位不會立刻啟動。一般模式仍是短按切換錄影、長按配對。

此開關透過保存＋重啟生效，不提供即時 BLE pause API。回滾或降級到 1.1 會忽略 1.2 的設定，不能依賴舊韌體維持 Link Pause。

## 更新與實機驗收

1.0 單槽安裝仍需先透過 ESP USB 安裝完整雙槽 layout；已安裝 1.1 雙槽 layout 可使用對應板型的 1.2 update ZIP。[更新流程](passthrough.md) 不變。

在 C3、C6 各記錄相機型號／韌體、FC 型號／Betaflight 版本與結果：

1. 先完成原 1.1 passthrough／更新／回滾驗收，再驗證 1.2；Action 2／Action 4／GoPro 支援狀態分別記錄。
2. 缺少新 key 的安裝保持兩行與原 USER；保存全部設定、重啟及完全斷電後讀回。比對 C3 的 5／25／100% 實際亮度。
3. 二→四→二行，確認行長、3/4 清除及錄影狀態不被警告遮蔽。對照相機畫面的時間、解析度／FPS、容量與單位；切換拍照、慢動作、未知資料及斷線不得顯示舊有效值。
4. 警告門檻上下反覆變動，確認遲滯、0% 有效電量及斷線清除。電量門檻測試須與相機回報一致，不用主機模擬代替實測。
5. 待機進入 Pause：BLE 中斷、無重連、OSD 清除、指示正確；斷電後仍暫停。管理器與 BOOT 分別恢復，配對保留，USER 高位不自動開始。錄影／saving／連線狀態不明時必須拒絕；完全離線可進入。
6. 保存與更新期間斷電、重試與錯板套件，驗證 NVS／配對保留、無假成功與 OTA 回滾。量測實際 free heap、最大可用區塊及 task stack；編譯大小不代表運行時記憶體充足。

軟體測試、審查修正與編譯結果見 [verification status](verification-status.md)。完成上述驗收後才能將此開發版列為硬體驗證完成。
