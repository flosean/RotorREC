# Betaflight 串口控制與 OSD

2026-09-12：ESP32 端已實作；尚未燒錄／接飛控實測。以下不是飛控設定已完成的宣告。

## 使用方式

用 Betaflight Modes 的 **USER1** 作為錄影開關：模式啟用＝開始，停用＝停止。任意 AUX 通道、開關方向及範圍都在 BF 設定，不需改 ESP32。此版不讀固定 RC 通道，也不綁 ARM。

開機／飛控串口失聯／RXLOSS／FAILSAFE 恢復後，先讓 USER1 停用至少 200 ms，再啟用才能開始控制。這個初始停用動作不會停止相機本來正在進行的錄影。正常控制期間，每次穩定的模式變化只產生一次明確開始／停止指令，失敗不自動反覆切換。

若切換時相機尚未就緒，保留目前開關的待執行意圖，等相機就緒且收到有效狀態後執行；新的開關位置會取消舊意圖。飛控資料失效則清除意圖，要求重新回到停用位置。相機本體／BOOT 手動操作仍可用；沒有新模式變化時，ESP32 不會持續強迫相機服從開關位置。

## 接線（目前預設）

下表是 C6-LCD-1.47。**C3-Zero 改用 GP0 TX、GP1 RX**，其 GPIO18/19 為 USB，不可照接下表；詳見 [C3 接線](esp32-c3-zero.md)。

| ESP32-C6-LCD-1.47 | 飛控 |
|---|---|
| GPIO18 / UART1 TX | 選定空閒 UART 的 RX |
| GPIO19 / UART1 RX | 同一 UART 的 TX |
| GND | GND |

- 3.3 V 邏輯、115200 baud、8N1、無反相／流量控制。USB 保留做供電與日誌。
- 第一次桌上測試讓 ESP32 由 USB 供電；不要把飛控電池電壓接到 GPIO，也不要在未確認供電電路前同時接多路電源。
- 飛控端是哪個 UART 不影響 ESP32 協議；但必須將那個埠設定為標準雙向 MSP，且 baud 相同。不要共用接收機／GPS／圖傳已使用的 UART。
- 不是把 ESP32 當 DisplayPort 圖傳。圖傳／眼鏡的既有 OSD 路徑保持原樣，ESP32 只向飛控寫 Custom Message。
- `menuconfig` → `RotorREC serial bridge` 可停用串口或調整 ESP32 接腳、baud、USER1～USER4（permanent ID 40～43）。本板 LCD／USB／BOOT 等占用脚位有程式檢查，不允許直接挪用。

## BF 端需要準備，但本次沒有自動修改

1. 在選定 UART 開啟 MSP 115200。ESP32 會確認 `BTFL` 與 MSP API major 1、minor >=47；未通過時不控制相機。
2. Modes 中將 **USER1** 指派給你要的 AUX 範圍。
3. 若 USER1 未出現，先檢查飛控是否編入 PINIO/USER 模式支援，以及現有 `pinio_box` / PINIO 資源用途。官方實作只公布有被 PinioBox 監看到的 USER 模式。**不要直接覆蓋整組 pinio_box 或挪用既有 PINIO**，某些飛控用它控制圖傳供電。為相機選未使用的 USER 模式，必要時同步調整 ESP32 的 USER mode ID。
4. 在 OSD 版面啟用並放置 **Custom Message 1** 和 **Custom Message 2**。第 3、4 行保持不動。

## OSD 顯示

| 行 | 例子／意義 |
|---|---|
| Custom Message 1 | `CAM REC`、`CAM STANDBY`、`CAM BUSY`、`CAM STATUS ?`、`CAM NO LINK` |
| Custom Message 2 | `CAM BAT 53%`；無有效資料時 `CAM BAT --` |

錄影狀態來自相機回報，不把開關位置或指令 ACK 當作錄影成功。Action 2 電量指的是目前已驗證的相機顯示百分比，不宣稱主機／螢幕模組分開電量。

變更時更新，重連時重送；另每 5 秒補寫一次 RAM 文字，處理飛控極快重啟、未被狀態輪詢察覺而遺失文字的情況。不寫 EEPROM。文字寫入需 ACK，失敗保留重試，拒絕則暫停 OSD 寫入直到重新建立 MSP session。

**限制：ESP32 完全斷電或 UART 線斷掉時，無法再通知飛控清除文字。BF 的 Custom Message 可能停留在最後一次內容；目前沒有飛控端 TTL，不能把靜止的 `CAM REC` 當成模組仍在線的證據。** BLE 失聯但 UART 正常時則能更新成 `CAM NO LINK`。故障條件仍需實測。

## 實作與驗證

- UART1 獨立任務；原生 MSPv2、CRC、分段接收、長度上限 256 bytes、雜訊恢復、150 ms 回覆逾時。約 10 Hz 狀態輪詢，錯誤／逾時時降速；一次僅有一個未完成請求。
- `MSP_BOXIDS` 每 5 秒重新取得，用 permanent ID 找模式在 `MSP_STATUS` 的實際位元。支援超過 32 個模式的延伸 flags，不硬編 bit 40。缺模式／格式不符時禁止控制。
- FAILSAFE 模式及 arming-disable 的 FAILSAFE、RX_FAILSAFE、BOXFAILSAFE 都會封鎖錄影變更。無效回覆／UART 逾時不視為停用開關。
- 飛控請求白名單只有 API_VERSION、FC_VARIANT、BOXIDS、STATUS、SET_TEXT。沒有 RC 注入、ARM、馬達、PID、飛控設定或 EEPROM 寫入。
- 相機命令由獨立短任務執行，不阻塞串口輪詢；BOOT 與外部錄影請求共用互斥門檻。Action 2 ACK 後等待新的 02/70；未驗證 02/80 不得覆蓋已確認的錄影狀態。
- 主機測試：`.\tools\test-betaflight.ps1`，需要 MSVC C Build Tools。涵蓋封包、雜訊、CRC、長度、模式順序／延伸 bit、失效旗標、20 輪開關、防重送、相機未就緒／忙碌、時間回繞、未知電量及 OSD 文字。
- ESP-IDF 建置：`.\tools\idf.ps1 -IdfArguments @('-B','build-modular','build')`。
- 尚未驗證：實際 UART 電氣連線、飛控 USER1 支援與 OSD 元素、相機 BLE 與 UART 同時運作的延遲／長時間穩定性、模組與 FC 重啟、R SDK 回歸。本次沒有燒錄或更動飛控。

## 桌上驗收（拆槳）

1. 確認 UART 連線日誌、API、USER 模式存在；相機就緒後，OSD 電量與相機對照。
2. 開關先停用，再啟用／停用 20 次，確認相機實際開始／停止及 OSD 跟隨，固定開關不反覆發令。
3. 相機本體開始／停止錄影，確認 OSD 顯示實際狀態。
4. 錄影中測試接收機失聯、UART 斷開及 BLE 斷線；確認不因遺失控制資料發送停止命令。恢復後先回停用位置，再測新指令。
5. 模組及 FC 分別重啟，確認不因開機時開關為啟用而誤開始／停止；記錄 OSD 凍結限制。

## 官方依據

- [BF 模式與 permanent IDs](https://betaflight.com/docs/wiki/guides/current/Modes)、[PinioBox 與 USER 模式顯示條件](https://betaflight.com/docs/wiki/guides/current/Pinio-and-PinioBox)。
- [MSP_BOXIDS 順序與 packed mode flags](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp_box.c)。
- [MSP_STATUS 格式、SET_TEXT 處理](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp.c)、[MSPv2 text 常數](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp_protocol_v2_betaflight.h)。
- [失效位元](https://github.com/betaflight/betaflight/blob/master/src/main/fc/runtime_config.h)、[RXLOSS 旗標設定](https://github.com/betaflight/betaflight/blob/master/src/main/flight/failsafe.c)。
- [Waveshare 本板資源](https://docs.waveshare.com/ESP32-C6-LCD-1.47/Resources-And-Documents)。

來源於 2026-09-12 查核。上游 master 不是使用者實機版本；實機驗收不可由原始碼查核代替。
