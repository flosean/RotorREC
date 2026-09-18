# ESP32-C3-Zero

RotorREC 1.0.0 的無螢幕板型，使用 Waveshare ESP32-C3-Zero（C3FH4、4 MB Flash）。與 C6 共用相機、Betaflight 與控制政策；不編入 LCD／LVGL。

## 操作

- BOOT（GPIO9）短按：就緒時切換錄影。
- BOOT 長按 1.2 秒：重新搜尋／配對。
- RESET：重啟模組。
- USB 日誌：顯示連線、電量與 R SDK 配對碼。
- GPIO10 RGB：依相機實際狀態顯示燈號。

| 燈號 | 意義 |
| --- | --- |
| 綠恆亮 | 已連線，已確認待機 |
| 綠慢閃 | 實際錄影中 |
| 紅慢閃 | 未連線、搜尋失敗或配對遭拒 |
| 紅快閃 | 搜尋、配對或重連中 |
| 黃快閃 | 等待有效狀態、指令確認或儲存中 |

慢閃為亮 1 秒、滅 1 秒；快閃為亮 0.2 秒、滅 0.2 秒。本板使用 RGB 序列順序，修正後燈色仍待肉眼驗收，不代表所有 WS2812 板型均使用相同順序。

## 接線

| C3-Zero | 接飛控 |
| --- | --- |
| GP0（UART1 TX） | 空閒 MSP UART RX |
| GP1（UART1 RX） | 同一 MSP UART TX |
| GND | GND |

115200 baud、8N1、3.3 V 邏輯、不反相。GP0／GP1 是 GPIO 編號，勿混淆排針序號。預設不使用 UART0 GP21／GP20，避免開機日誌混入 MSP；也不能沿用 C6 GPIO18／19，因為 C3 使用這兩腳作 USB。

首次由 USB 供電，勿將電池電壓或 5 V 接到 GPIO。GPIO2／8／9 涉及啟動，GPIO10 接 LED，GPIO12～17 接 Flash，程式禁止將它們設定成 BF 串口腳位。

## 建置與燒錄

先開啟 ESP-IDF PowerShell 終端機：

```powershell
.\tools\c3-zero.ps1 -IdfArguments @('build')
.\tools\c3-zero.ps1 -IdfArguments @('menuconfig')
.\tools\c3-zero.ps1 -IdfArguments @('-p','COMxx','flash','monitor')
```

將 COMxx 換成實際埠。wrapper 使用 `build-c3-zero/`、該目錄的 sdkconfig 與 `config/sdkconfig.c3-zero.defaults`，不改 C6 設定。應用映像為 `build-c3-zero/rotorrec.bin`；透過 flash 指令一起寫入 bootloader 和 partition table。

其他系統可使用 [建置文件](build-and-flash.md) 的 idf.py 指令。請勿把 C6 映像燒入 C3。若不能自動下載，按住 BOOT，按一下 RESET 再放開 BOOT；下載模式的埠號可能改變。退出 monitor 使用 Ctrl+]。

## 首次配對與驗收

1. 先只插 USB，確認日誌顯示 C3 板型與 UART TX0／RX1。
2. 關閉其他相機遙控器，將相機放近模組並開機。
3. 首次搜尋先嘗試公開 R SDK 再轉 Action 2；如相機出現確認提示，在相機接受。搜尋失敗可長按 BOOT 重試。
4. 確認相機藍牙標示、實際錄影切換與電量，再測雙方各自重啟、超距離恢復。
5. 拆槳接飛控，依 [Betaflight 設定](betaflight-setup.md) 驗證 USER1 與 OSD。

C3 已有 Action 2 待機／電量回報與燒錄證據；完整操作、RGB 與 BF 驗收狀態見 [驗證狀態](verification-status.md)。

硬體來源：[Waveshare C3-Zero 文件](https://docs.waveshare.com/ESP32-C3-Zero)。
