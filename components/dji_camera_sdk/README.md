# DJI camera SDK component

來源：DJI Osmo GPS Controller Demo，本機快照日期 2026-08-24。原始授權見 `LICENSE`。

本專案只編入 CRC、protocol、BLE、data，以及 connect/command/status/enums logic。未使用官方範例的 GPS、按鍵、LED 與測試程式。

相對官方範例的本機修改集中在：

- `ble/ble.c`、`ble/ble.h`：Action 2 GATT 診斷、FFF3、BLE address type、指定 MAC 重連與 disconnect callback。
- `data/data.c`：傳輸 frame 診斷 log。

