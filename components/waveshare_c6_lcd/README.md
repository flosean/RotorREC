# Waveshare ESP32-C6-LCD-1.47 component

Source: the minimal LCD/LVGL subset of the Waveshare `ESP32-C6-LCD-1.47-Test` demo, imported on 2026-08-24.

Local board fixes verified on hardware:

- Initialize SPI2 with `spi_bus_initialize(SPI2_HOST, ...)`.
- Use a 40 MHz LCD SPI clock.
- Configure the physical 172x320 panel as a 320x172 application display using `LV_DISP_ROT_270`.
- Apply the 34-pixel offset to the Y axis after rotation.
- Use a 320x20 LVGL draw buffer.

MicroSD, Wi-Fi, RGB, and example UI applications are excluded. Source comments are translated into English; existing copyright and license headers are retained.
