# RotorREC 完整使用流程與驗收

更新：2026-10-22；適用 1.2.0-dev。這份文件把首次安裝、日常錄影、維護與故障復原串成同一條流程。此次依使用者要求只完成軟體檢查，未接測試硬體、未刷寫，也未執行相機錄影或斷電測試。軟體證據見 [verification status](verification-status.md)。

## 1. 確認設備與連接方式

| 項目 | 要確認的內容 |
| --- | --- |
| ESP32 | Waveshare C3-Zero 4 MB，或 C6-LCD-1.47 8 MB；不能只因同為 ESP32 就混用映像 |
| 飛控 | Betaflight 2025.12+、MSP API 1.47+；保留目前設定備份、板型與韌體版本 |
| 相機 | Action 2 選 `action2`；Action 4 的既有公版協定路徑選 `dji-rsdk`；GoPro 選 `gopro`，仍待指定型號實測；Nano 尚無啟用的轉接器 |
| 相機準備 | 電量足夠、儲存空間可用、能用相機本身按鈕正常產生影片；R SDK／GoPro 使用錄影模式 |
| UART | 空閒、雙向、非反相、3.3 V 邏輯；MSP 115200 8N1，不能與接收機／GPS／VTX 共用 |

USB 的用途不同：

| 電腦連到哪裡 | 用途 | 工具 |
| --- | --- | --- |
| ESP32 的 USB | 首次完整安裝、舊分割區遷移、救援、韌體日誌 | ESP-IDF `flash`／`monitor` |
| 飛控 USB | 設定飛控的 Ports、Modes、OSD | Betaflight Configurator |
| 飛控 USB | 選相機、對頻、RotorREC 設定與後續更新 | RotorREC manager；先關閉 Configurator |

Manager 的 `--direct` 是「飛控已在 UART passthrough」，不是 ESP32 USB 日誌介面。正常錄影不需要連接電腦。

接線時斷電，台架測試拆槳。C3：GP0 → FC RX、GP1 ← FC TX；C6：GPIO18 → FC RX、GPIO19 ← FC TX；兩者都要共地。第一次以 ESP USB 供電。之後若改由飛控供電，依實際板卡規格接穩壓電源；不可把電池電壓或 5 V 接到 UART GPIO，也不要在未確認電源電路前並聯供電。部分飛控 USB 不供電給周邊。

## 2. 首次刷寫 ESP32

使用 ESP-IDF v5.5.5 的初始化終端，在專案根目錄執行：

```powershell
.\tools\passthrough.ps1 -Board c3
.\tools\passthrough.ps1 -Board c3 -IdfArguments @('-p','COMxx','flash','monitor')
```

C6 改成 `-Board c6`；`COMxx` 必須是 ESP32 的連接埠。第一次和 1.0 升級必須用完整 `flash`，它寫入 bootloader、分割表、OTA metadata 和 app。單獨 app `.bin` 或 manager 更新 ZIP 都不能完成舊分割區遷移。正常完整刷寫保留 NVS；不要把 `erase-flash` 當成日常更新步驟。

成功判準：燒錄與驗證無錯誤、重新開機日誌顯示 `RotorREC 1.2.0-dev` 以及正確 UART 腳位；C3 出現狀態燈／C6 顯示畫面，沒有不斷重開機。沒有相機時搜尋失敗不等於刷寫失敗。

自動進入下載模式失敗時，按住 BOOT、按下並放開 RESET，再放開 BOOT；重新查看連接埠，有些系統會換埠。離開 monitor 用 Ctrl+]。詳細指令見 [build and flash](build-and-flash.md)。

## 3. 設定 Betaflight

1. 使用 Configurator 備份現有設定。Ports 中只對接 RotorREC 的 UART 啟用一般 MSP，速率 115200，儲存並重新啟動。
2. Modes 中把預定 AUX 開關範圍分配給 USER1；撥動開關，確認 USER1 的啟用狀態確實改變。選 USER2–4 時，manager 也要選相同 USER。
3. USER 模式不存在時，先在 CLI 讀取 `get pinio_box` 與 `resource`，確認韌體具有 PINIOBOX，並找未使用的項目。USER1–4 的 permanent ID 是 40–43。不要覆蓋既有 VTX 電源等用途。
4. 例如**僅在原值為 `255,255,255,255`，且第一個 PINIO 資源沒有其他用途**時，可設定 `set pinio_box = 40,255,255,255` 再 `save`，使 USER1 出現。已有其他配置時只修改已確認空閒的一項、保留其餘原值；四項都在使用時先解決配置衝突。RotorREC 讀取模式，不需要另外把一支實體 GPIO 指派給 USER。
5. OSD 中啟用並擺放 Custom Message 1、2；選四行模式時另加 3、4。確認作用中的 OSD profile 及眼鏡／影像傳輸的既有 OSD 路徑。RotorREC 不是 DisplayPort 裝置。
6. 離開 Configurator。開關先保持 inactive 至少 200 ms；等待實際相機狀態有效後再啟用。

成功判準：ESP USB 日誌顯示 Betaflight API 與 USER 對應成功，實際眼鏡 OSD 顯示相機狀態與電量。模式超過 32 項時，韌體會依序讀取所有 BOXIDS 頁面；任一頁讀取失敗都不允許使用不完整的模式對應。

USER/PINIO 的來源是 [Betaflight PinioBox 文件](https://betaflight.com/docs/wiki/guides/current/Pinio-and-PinioBox)；分頁行為核對自 [MSP BOXIDS 實作](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp.c) 及 [每頁 32 項的序列化](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp_box.c)。2026-10-22 核對的上游程式不代表每一塊飛控都具有相同編譯功能。

## 4. 選相機與對頻

```powershell
python -m pip install pyserial==3.5
python tools/rotorrec_manager.py gui
```

GUI 需要 Python 3.10+、tkinter 和 pyserial。關閉占用序列埠的工具，選飛控 USB 埠。UART 填飛控板上的 UART 編號，例如 UART2 填 `2`；留白僅在恰好一個硬體 UART 啟用 MSP 時自動選擇。

1. 按 **Read status**，確認相機協定與設定讀回成功。全新安裝預設 Action 2，不會自動跨品牌切換；USB 更新保留 NVS，所以舊裝置可能仍是其他選擇。
2. 要改協定時，選 `action2`／`dji-rsdk`／`gopro`，按 **Save camera & restart**，等待重新連接與設定讀回。
3. 讓目標相機開機且可被 Bluetooth 發現，關閉其他相機的配對模式及競爭連線的遙控器／App。第一次只讓預定相機處於配對狀態。
4. 等待啟動搜尋；需要重新搜尋時按 **Pair camera**。Action 2 接受相機上的配對提示；R SDK 核對 manager／C6 畫面／C3 USB 日誌的四位數代碼，再於相機確認。GoPro 先進入 App／Bluetooth 配對模式，並選 Video preset group。
5. Manager 可等候最多約 120 秒的狀態輪詢；傳輸中斷另有回應逾時。Connected 代表連線完成，還要確認錄影狀態、電量有效，才能算可操作。
6. 維護完成後關閉 manager，**讓飛控真正斷電重啟**（包含仍供電的 USB／電池）。只重啟 ESP32 不會退出飛控 passthrough；這一點也符合 [Betaflight passthrough 實作](https://github.com/betaflight/betaflight/blob/master/src/main/cli/cli.c)。重啟後先讓 USER inactive，再驗證 OSD／開關控制。

第一次 DJI 搜尋失敗且沒有儲存目標時，不會無限自動搜尋；相機準備好後按 Pair camera，或離開管理狀態後長按 BOOT 1.2 秒。已儲存目標會自動重連。R SDK 暫時通訊／版本查詢／訂閱失敗會重試；相機明確拒絕後則等待人工重新配對。GoPro 自動重試搜尋，詳細限制見 [GoPro setup](gopro.md)。

重新配對會在 GoPro 路徑清除原 GoPro 目標／bond；DJI 路徑重新搜尋，成功後才替換目標。不同協定的已存目標分開保存。切換相機協定不會讓其他協定的目標消失。

## 5. 日常操作與成功判準

| 操作／狀態 | 預期結果 |
| --- | --- |
| 相機與 ESP32 開機 | 已配對相機自動重連；等候實際錄影狀態 |
| 開機時 USER 已啟用 | 不會直接發送錄影指令；先 inactive 至少 200 ms 再切 active |
| USER inactive → active | 一次開始錄影請求；確認相機及 OSD 顯示 REC |
| USER active → inactive | 一次停止錄影請求；等相機存檔，再確認待機 |
| BOOT 短按 | 就緒且錄影狀態有效時切換開始／停止；Action 2 session setup／rejected 時另可觸發既有配對重試 |
| 相機本身按鍵 | OSD 跟隨相機真實狀態；沒有新的 USER 邊緣時，不會強制切回開關狀態 |
| BOOT 長按 1.2 秒 | 空閒／離線時重新對頻；Link Pause 時恢復連線；錄影、存檔、指令處理中或已連線但狀態未知時不重新對頻 |
| `CAM STATUS ?`／LCD `STATUS UNKNOWN` | 尚無可用錄影狀態；R SDK／GoPro 檢查錄影模式，不能當作待機或 REC |
| `CAM BUSY`／黃色快閃 | 等待相機完成存檔或工作，避免連續操作 |
| `CAM NO LINK`／紅燈 | 檢查相機電源、距離、配對與協定；UART 正常時 BLE 斷線會更新 OSD |

C3 綠燈恆亮是確認待機，慢閃是錄影；紅燈快閃是搜尋／對頻／重連，慢閃是離線／失敗；黃色快閃代表狀態未知、指令或存檔中；藍燈恆亮是 Link Pause。實際顏色仍須在板上確認。

命令 ACK 不等於影片已產生。首次測試須用 BOOT、USER 及相機按鍵各錄一段，停止後檢查檔案可播放。不要用連續撥動或反覆按鍵來處理未知結果。

## 6. 斷線、維護與更新

| 情境 | 行為與復原方式 |
| --- | --- |
| BLE 離線 | 不因斷線自動停止錄影；已保存目標嘗試重連。有效 FC 輸入下，尚未執行的最新 USER 意圖可能在相機恢復後執行，因此重連時確認開關與相機狀態 |
| UART、接收機或 FAILSAFE 異常 | 清除待執行 USER 意圖；不把失聯當作停止指令；恢復後先 inactive，再產生新切換 |
| ESP32 斷電／UART 拔除 | FC 可能保留最後一筆 OSD 文字；靜止的 `CAM REC` 不能證明模組仍在線 |
| 管理操作進行中 | USER／BOOT 相機操作暫停，不會主動 STOP；不要在此期間用相機按鍵開始錄影 |
| 錄影／存檔／狀態未知時存設定或更新遭拒 | 先在相機停止並等狀態確認；相機處於 Photo 等未知狀態時改回 Video。需要離線救援時先安全停止錄影，再關閉相機 |
| 關閉 GUI 後開關／OSD 無更新 | 飛控可能仍在 passthrough，完整重啟飛控 |
| 換 USB 埠／UART | GUI 會清除 already-in-passthrough 與設定讀取資格；重新 Read status。若該目標本來已在 passthrough，手動勾選該選項 |
| 同一飛控斷電重啟 | 手動取消 already-in-passthrough，再 Read status |
| 前次管理連線中斷 | 原 session 最多保留約 30 秒；等待後重連，或重啟飛控回到一般模式 |
| Link Pause | 保留配對、關閉 BLE、清空自有 OSD 行；由 manager 恢復或 BOOT 長按恢復；恢復後重建 USER inactive 基準 |

修改 USER／OSD／LED／低電量設定前先 Read status，再 Save settings & restart，等待讀回驗證。四行模式還需在 BF 放置 Custom Message 3/4；曾啟用四行後，即使改回兩行，RotorREC 仍會清空自己已占用的 3/4，避免留下舊資料。

更新須先建置正確板型，並用 manager `pack` 產生 ZIP；不能把首次 USB 安裝包當作 app 更新包。選 Update firmware，等待傳輸、重啟、版本與 ELF fingerprint 驗證全部完成。100% 傳輸完成還不是更新完成。細節見 [passthrough](passthrough.md)。

取消／傳輸中斷：在映像尚未提交前，原啟動映像維持不變，下一次從頭傳輸。提交後若回覆丟失，裝置可能在 session 到期後啟動新映像；先 Read status 查版本與 fingerprint，不要推定結果。新映像的早期啟動失敗可由雙槽 rollback 復原，但不涵蓋所有後期錯誤。若 bootloader／分割表受損或沒有可啟動 app，改由 ESP USB 完整刷寫救援。

儲存損毀時不要反覆擦除：先保存日誌，檢查電源與正確映像。完整 erase-flash 會刪掉設定、配對及 BLE bond，只能在接受資料遺失後作為最後救援方式，且之後須重新安裝及對頻。

## 7. 實機驗收表（此次尚未執行）

每次填入板型、韌體 ELF fingerprint、FC 型號／BF 版本、相機型號／韌體版本和日期；C3、C6 分別執行，不能以其中一款替代另一款。

- [ ] 全新 USB 安裝與 1.0 分割區遷移；既有配對保留，日誌無重啟迴圈。
- [ ] 相機早於／晚於 ESP 開機；第一次對頻、接受／拒絕提示、搜尋失敗後人工重試。
- [ ] 相機與 ESP 分別重啟各 10 次；保存目標能恢復，換協定再切回仍找到原目標。
- [ ] BOOT／USER 各連續開始停止 20 次；OSD／LCD／LED 與相機一致，影片可播放。
- [ ] 相機按鍵手動錄影、Photo 模式、存檔中、滿卡、低電量、未插卡；失敗不偽報錄影成功。
- [ ] USER1–4 對應、AUX 範圍與開機 active 情境；大量已啟用模式時，跨 BOXIDS 分頁仍可控制。
- [ ] 錄影中 BLE／UART／接收機中斷；重連後不出現非預期重複 shutter，並核對延後 USER 意圖。
- [ ] ESP／FC 分別重啟；實際眼鏡驗證 OSD 的更新與失聯殘留限制。
- [ ] 管理連入、相機切換、儲存設定、Link Pause 跨斷電保存及 BOOT 恢復；離開後完整重啟 FC。
- [ ] 正確／錯誤板型更新包、取消、斷線、更新中斷電、第一次啟動失敗 rollback、ESP USB 救援。
- [ ] 至少 10 分鐘閒置 keepalive；長時間錄影、BLE 負載與 heap 觀察；C3 顏色／C6 實際排版確認。

目前可以確認軟體修正與自動化檢查結果；這份清單未完成前，不能宣稱所有相機、板卡與故障情境都已實機通過。
