# Waveshare ESP32-C6-LCD-1.47 component

來源：Waveshare `ESP32-C6-LCD-1.47-Test` 官方 demo 的最小 LCD/LVGL 子集，本機快照日期 2026-08-24。

此副本包含本機實測修正：

- `spi_bus_initialize(SPI2_HOST, ...)` 必須執行。
- LCD SPI clock 40 MHz。
- 實體 172×320，應用在 `LV_DISP_ROT_270` 使用 320×172。
- 旋轉時 34-pixel panel offset 加到 Y 軸。
- LVGL draw buffer 為 320×20 pixels。

未包含 MicroSD、Wi-Fi demo、RGB demo 與範例 UI。
