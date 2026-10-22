# RotorREC Chrome Configurator

透過桌面 Chrome / Edge 的 Web Serial，從飛控 USB 管理 RotorREC。這是可直接運行的靜態網頁，沒有 npm 安裝、雲端服務或韌體上傳。

在專案根目錄啟動僅限本機的伺服器：

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory tools/web-configurator
```

以 Chrome 開啟 [http://localhost:8000](http://localhost:8000)。也可將這個目錄放在 HTTPS 靜態網站；不要直接以 `file://` 開啟。瀏覽器需要 Web Serial、Web Crypto 與 `DecompressionStream('deflate-raw')`。不使用已淘汰的 Chrome Apps 封裝。

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

## 測試與範圍

```powershell
node --test tests/test_web_configurator.mjs
```

從專案根目錄執行。測試涵蓋 Python golden wire 相容性、CRC 與分段封包、設定編碼、UART 選擇、ZIP / 映像驗證、實際 JS Client 刷寫與重送、取消、錯板、遺失 END 回應、重啟指紋及 Web Serial stream 關閉。實際 FC / USB / UART、相機、Flash 斷電與回滾仍須依 [實機驗收](../../docs/passthrough.md) 在 C3 / C6 執行。

完整 GUI 的模擬裝置回歸可從專案根目錄執行 `python -m http.server 8001 --bind 127.0.0.1`，開啟 `http://127.0.0.1:8001/tests/web-configurator-ui.html`。頁面會自動操作實際 GUI，使用模擬 Web Serial streams 驗證連線、設定、配對、刷寫、取消與 USB 中斷；完成顯示 `ALL UI CHECKS PASSED`。此測試不要求串口權限，也不會接觸實體裝置。

依據：[Chrome Web Serial](https://developer.chrome.com/docs/capabilities/serial)、[Betaflight serial CLI 實作](https://github.com/betaflight/betaflight/blob/master/src/main/cli/cli.c)。
