# RotorREC Configurator

透過桌面 Chrome / Edge 的 Web Serial，從飛控 USB 管理 RotorREC。這是可直接運行的靜態網頁，沒有 npm 安裝、雲端服務或韌體上傳。

目前開發版也把同一介面放進模組 Wi-Fi AP，供手機地面維護。首次刷寫仍需 ESP USB；Wi-Fi 不提供相機 BLE 控制或飛控配置。軟體模擬通過不代表實際無線、供電或 Flash 斷電驗收。

在專案根目錄啟動僅限本機的伺服器：

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory tools/web-configurator
```

以 Chrome 開啟 [http://localhost:8000](http://localhost:8000)。也可將這個目錄放在 HTTPS 靜態網站；不要直接以 `file://` 開啟。桌面連線需要 Web Serial；更新 ZIP 需要 `DecompressionStream('deflate-raw')`。模組本機 HTTP 使用經測試的 SHA-256 備援，不要求 Web Crypto digest。

## 使用

1. 拆下螺旋槳，關閉占用序列埠的 Betaflight Configurator。確認 RotorREC 有供電；部分飛控 USB 不供電給周邊。
2. RotorREC UART 須先在 Betaflight 啟用 MSP 115200。填寫實際 UART 編號（例如 UART2 填 `2`）；留空只有在恰好一個硬體 UART 啟用 MSP 時才自動選取。
3. 按「選擇序列埠並連線」，在瀏覽器權限視窗選擇飛控。程式確認 Betaflight、讀取 UART 配置後啟用透傳，再讀取 RotorREC。
4. 選相機協定、重新配對，或調整 USER、OSD、LED、低電量門檻及 Link Pause。保存後會重啟 ESP，讀回確認。配對碼顯示於頁面，依相機畫面確認。
5. 韌體更新使用 `rotorrec_manager.py pack` 產生的 ZIP。先檢查完整 ZIP、CRC、SHA-256、app 描述，再比對裝置板型、分區與容量；只寫入備用 app 分區。傳送完成後會驗證新韌體版本與 ELF 指紋。
6. 完成後斷線，將**飛控重新上電**以恢復 MSP、OSD 與 USER 錄影控制。重啟 ESP 不會退出飛控透傳。

當同一飛控未重新上電且仍在透傳，勾選「已在 Passthrough」重新連線。換飛控或重新上電後請取消勾選。若上次回應逾時，等待至少 30 秒讓舊管理 session 到期；重新讀取狀態確認結果。瀏覽器分頁保持前景，避免電腦睡眠；不要在刷寫期間重新整理或關閉分頁。

「取消刷寫」會等待目前封包 ACK，並在提交前中止；提交及開機驗證開始後無法取消。END 回應全部遺失時，工具改用新 session 等待裝置自動重啟並驗證結果；不向舊 session 發送 ABORT。無法確認時明確報告結果不明，不宣稱已刷好或已回復。

首次安裝、舊 1.0 單槽系統或 bootloader / 分區表損壞仍需 ESP USB。此工具不刷寫飛控韌體，不提供 ESP ROM downloader。相機選項是已實作的協定路徑，不代表所有同品牌機型均已驗證。

## 錄影、OSD 與重設

- 錄影模式：USER 持續開關、ARM、Button 瞬時按鍵、Combi 或 Off。Button／Combi 使用選定 USER 的 LOW→HIGH 邊緣；先在 Betaflight Modes 指派瞬時按鍵並回 LOW。ARM／Combi 可分別開關自動開始與停止、設定 0–30 秒停錄延遲。重新 ARM 取消倒數，直接手動停止不等待倒數。
- 計時與警告依相機能力：Action 2 的 `~` 計時從確認 OFF→ON 起算，未知超過 5 秒清除；相機原本已在錄影時不猜起點。GoPro 額外查詢不支援時退回基本狀態。未知值不是零，也不能推論無卡／溫度正常。
- 四行模板可用 `{state}`、`{battery}`、`{elapsed}`、`{remaining}`、`{format}`、`{space}`、`{mode}`，每行最多顯示 16 格；留空保留內建行。重要警告與倒數優先佔第一行。
- 清除配對會刪除全部三種相機協定的保存資料與 BLE bonds，保留設定並開啟 Link Pause。恢復出廠另清除設定、OSD、待停錄工作與 Wi-Fi 密碼。介面需勾選確認；有線操作必須重啟讀回 `reset_pending=0`、無儲存錯誤才報完成。Wi-Fi 出廠重設後失去連線，只報要求已保存，需重新連接驗證。

## 手機 Wi-Fi 流程

1. 首次先由有線工具顯示並保存模組的熱點名稱與隨機密碼。C6 也可於 Wi-Fi 模式從 LCD 讀取；C3 USB console 會輸出連線資訊。
2. 讓相機停錄、飛控上鎖；按網頁「重啟進入 Wi-Fi 維護」，或在正常開機後按住 BOOT **8 秒並放開**。不要按著 BOOT 上電，該操作可能進入 ESP 下載模式。一般長按 1.2–8 秒後放開仍是重新配對／恢復 Link Pause。
3. 手機加入 `RotorREC-XXXXXX`，選擇保持無網際網路的連線，開啟 `http://192.168.4.1`。一次只容許一個連線裝置。請使用支援上述 ZIP 解壓縮能力的瀏覽器。
4. Wi-Fi 模式不啟動 BLE／飛控控制；可改設定、模板、更新 ZIP 與重設，但配對須退出後進行。保存設定或更新重啟後會再次進入 Wi-Fi 供讀回。
5. 按「退出 Wi-Fi，恢復運行」，或長按 BOOT 後放開，返回正常模式。十分鐘無操作且無進行中工作會退出；一般斷電重開亦退出。更新中拒絕按鍵退出，避免中斷 Flash 工作。
6. 若曾從飛控 USB passthrough 進入，仍須重新上電飛控；僅重啟模組無法退出 FC passthrough。Wi-Fi 期間沒有持續 ARM 監測，不是禁止飛控解鎖的安全鎖。

Wi-Fi 熱點啟動失敗會重啟回正常模式。通訊遺失時不產生假 ACK、不換序號重送不同命令；重新連線後先讀狀態。診斷匯出不包含 Wi-Fi 密碼。

## Betaflight OSD 配置

從主頁開啟 `bf-setup.html`。先關閉管理連線並重新上電飛控，再連接 FC USB。工具只接受已核對的 BTFL API 1.47／1.48，讀取模式、畫布與 OSD 設定檔。產生四行提案、預覽、勾選確認後才寫 Custom Message 1–4；每次寫入和 EEPROM 保存前重新檢查 ARM／FAILSAFE，保存前後均讀回。其他 OSD 元件與設定檔顯示位元保留。

UART 與 USER 指派仍由 Betaflight Configurator 設定。發生中斷或部分寫入時不要假定已保存；重新上電、重讀並檢查眼鏡畫面。位置不能超出畫布，四行不能互相重疊；其他 OSD 元件的避讓仍需人員確認。

## 軟體回歸

```powershell
node --test tests/test_web_configurator.mjs tests/test_bf_setup.mjs
```

從專案根目錄執行。測試涵蓋 Python golden wire 相容性、CRC 與分段封包、設定編碼、UART 選擇、ZIP / 映像驗證、實際 JS Client 刷寫與重送、取消、錯板、遺失 END 回應、重啟指紋及 Web Serial stream 關閉。實際 FC / USB / UART、相機、Flash 斷電與回滾仍須依 [實機驗收](../../docs/passthrough.md) 在 C3 / C6 執行。

完整 GUI 的模擬裝置回歸可從專案根目錄執行 `python -m http.server 8001 --bind 127.0.0.1`，開啟 `http://127.0.0.1:8001/tests/web-configurator-ui.html`。頁面會自動操作實際 GUI，使用模擬 Web Serial streams 驗證連線、設定、配對、刷寫、取消與 USB 中斷；完成顯示 `ALL UI CHECKS PASSED`。此測試不要求串口權限，也不會接觸實體裝置。

加上 `?transport=wifi` 可執行 390px 寬度的 HTTP 管理流程，包含重設結果界線；`/tests/bf-setup-ui.html` 驗證實際 BF 設定頁的開埠失敗、ARM 阻擋、明確套用與 USB 恢復。

依據：[Chrome Web Serial](https://developer.chrome.com/docs/capabilities/serial)、[Betaflight serial CLI 實作](https://github.com/betaflight/betaflight/blob/master/src/main/cli/cli.c)。
