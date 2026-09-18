# Third-party notices

## DJI camera SDK

`components/dji_camera_sdk/` contains a modified subset of the DJI Osmo GPS Controller Demo. The upstream notice, including its MIT license text and references to DJI terms, is preserved verbatim in [LICENSE](components/dji_camera_sdk/LICENSE). Local modifications are described in the component README and source files.

## Waveshare LCD component

`components/waveshare_c6_lcd/` contains the LCD/LVGL subset of the Waveshare ESP32-C6-LCD-1.47 demo, with local board fixes. Existing source headers are retained. No separate upstream license file was present in the imported subset; this project does not assign it a replacement license. See the component README for provenance.

## Build dependencies

ESP-IDF is supplied by the developer's SDK installation. LVGL is fetched by ESP-IDF Component Manager for the C6 build, using `main/idf_component.yml` and `dependencies.lock`. Downloaded dependency trees are not committed; their upstream notices remain with those packages.

## RotorREC original code

No project-wide license has been selected for the original code in this initial publication. Third-party notices do not grant a blanket license to the entire repository.

The Espressif-authored ST7789 driver files retain Apache-2.0 SPDX headers. The corresponding license text is included as components/waveshare_c6_lcd/LICENSE.Apache-2.0; this does not relicense other Waveshare files.
