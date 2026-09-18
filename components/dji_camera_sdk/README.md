# DJI camera SDK component

Source: DJI Osmo GPS Controller Demo, imported on 2026-08-24. Original license notices are preserved in `LICENSE`.

The build includes CRC, protocol, BLE, data, and connection/command/status/enum logic. The upstream GPS application, button and LED examples, and test programs are not included in the build.

Local changes include:

- `ble/ble.c` and `ble/ble.h`: Action 2 GATT diagnostics, FFF3 support, BLE address type handling, reconnection to a specific MAC address, and the disconnect callback.
- `data/data.c`: frame transmission diagnostics.
- English translations of comments, diagnostics, and camera mode labels. Original copyright and license notices are retained.
