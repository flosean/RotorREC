# RotorREC verification status

Updated: 2026-10-25. Firmware version: 1.2.0-dev (unreleased; new hardware acceptance pending).

## 2026-10-25 recording and setup software completion

The four requested areas (recording failure feedback, ARM/stop delay, camera telemetry/warnings, setup convenience) passed the [eight-part completion audit](recording-completion-plan.md). This includes the phone Wi-Fi interface and update path, OSD templates, separate desktop BF OSD placement, diagnostics, pairing removal and factory reset. No physical serial port, camera or FC was accessed. Historical results below are not substitutes for this version's hardware acceptance.

- Native management/reset/Wi-Fi-server/BOOT tests pass, including storage/OTA failures, every reset commit boundary, malformed HTTP frames, token/origin checks, timeouts, startup failures, idle exit and release-only button thresholds. A failed STOP save remains visible after reading the old value, until a successful save.
- Betaflight reports 11,966 checks (assertions, not independent scenarios); LCD, recording feedback, automation and telemetry suites also pass. Actual DJI callbacks/handshake/DUML/controller and GoPro protocol suites pass with simulated transports.
- Python 25 tests, Node 24 tests (17 manager, 7 BF setup), and the Tk GUI regression pass. The actual Web Serial and 390px Wi-Fi interface simulations each pass 18 checks; BF UI passes 9, including ARM after preview and USB exception recovery. These do not exercise actual RF or hardware drivers.
- ESP-IDF 5.5.5 builds pass after the final firmware changes. C3 app: 1,568,624 / 2,031,616 bytes; C6: 2,037,264 / 4,128,768 bytes. Slot space remains 23% / 51%; this is not a runtime heap measurement.
- `build-audit-c3/rotorrec-c3-update.zip` and `build-audit-c6/rotorrec-c6-update.zip` were regenerated and loaded by both real Python and JavaScript package readers. Manifest, image bytes, board, size, SHA-256 and ELF fingerprint agree. Packages are local and unflashed.
- Current user-flow, camera, GoPro, Betaflight and maintenance documents have been reconciled with implementation. Local Markdown links and `git diff --check` pass. No commit, push or release publication was performed.

| Final app | SHA-256 | ELF fingerprint |
| --- | --- | --- |
| C3 | `5c93da31bf723c9346c588e7e2790055b30b2dd313497dbf85dcdcc31b5aef78` | `32758f5b1df16f8b4697f61d31ae83a5de82a4e35e83c930c14cec413c1938e6` |
| C6 | `1ba6796ec8d095b681a128f644cbf30de7d711a77ae52217b26da10ad9072494` | `5cd7d88bf46621050cd2c6bfc1aecf85a56771b832fd1ddebe4494510a7eb0c5` |

Remaining physical acceptance: exact camera/firmware compatibility, real radio timing and phone browser behavior on the AP, recording files, FC passthrough recovery, OSD/LCD/LED appearance, runtime heap, power-loss persistence and bootloader rollback on both boards. The user requested software-only completion while hardware is unavailable. Wi-Fi maintenance disables BLE/FC control; BF placement uses desktop USB, and Wi-Fi factory reset cannot verify completion over credentials it has just erased.

**The project owner confirms that Betaflight UART communication and OSD functionality have been tested on real hardware, used successfully, and work correctly.** This replaces the earlier pending-hardware status for UART and OSD. The confirmation did not include a specific FC firmware/board combination or a detailed cycle-count and fault-injection record.

## 2026-10-22 complete user-flow software audit

The subsequent [passthrough and Chrome configurator audit](passthrough-audit.md) adds the browser manager, fragmented CLI and lost-final-ACK recovery checks, 20 Python tests, 11 JavaScript tests, browser UI integration and current passthrough build packages. It preserves the hardware acceptance boundaries below.

Scope: initial USB installation/migration, wiring, Betaflight configuration, camera selection/pairing, BOOT/USER recording, status displays, maintenance, UART updates and recovery. Baseline: `c30200f`. The user explicitly confirmed that no test hardware was connected and requested software checks first. No device was opened, flashed, restarted or commanded to record. The [complete user guide](user-journey.md) provides success criteria, troubleshooting and the pending physical acceptance checklist.

### Confirmed issues repaired

| Issue / user impact | Repair and evidence |
| --- | --- |
| BOXIDS read only page zero: USER modes beyond the first 32 enabled modes could never control the camera | Read all 32-ID pages, reject incomplete/oversized maps, then map the full list to STATUS flags. Host regression covers USER at bit 40, exact full pages, 128-entry bound, missing page and duplicate/missing IDs. Page transport is build checked; actual UART timing remains a hardware check |
| R SDK transient handshake/version/subscription errors became permanent pairing rejection | Preserve explicit rejection separately from transport failures, allowing the existing saved-target worker to retry. Actual handshake, adapter and controller worker are tested with substituted transport/RTOS; explicit rejection still waits for manual pairing |
| BOOT long press bypassed the management recording/busy guard | Both paths now use the same busy check. Native receiver tests cover recording, saving, pending commands, connected unknown state, idle/offline pairing and paused resume |
| C6 R SDK display could label invalid photo/unknown state as REC or standby | Gate LCD recording text on valid recording state and pending/busy flags. Actual dashboard text tested with substituted LVGL; physical pixels/fonts/layout are not simulated |
| Manager camera selector retained the default rather than the connected device; passthrough checkbox carried into a different port/UART | Populate selection from status and reset passthrough on connection edits. Hidden Tk regression exercises the actual GUI and worker queue |
| Manager pairing deadline was shorter than the combined DJI scan/GATT/approval bounds | Use a 120-second monotonic deadline; simulated 70-second pairing completes and closes its session. Actual radio timings remain unverified |
| Manager could show standby during saving/pending work and a fictitious numeric GoPro pairing code | Display busy/pending status and restrict numeric codes to DJI; Python regression covers both |
| Setup documents contained old version, automatic protocol detection and configurable pin claims | Correct version, runtime selection, fixed update-board wiring, paused LED/button behavior and maintenance recovery instructions; add the end-to-end guide |

### Verification results

- Betaflight suite: **11,965 checks passed**, including the new paginated mode reader. This is an assertion count, not 11,965 independent user scenarios.
- GoPro protocol/status/advertisement/integration suite passed.
- DJI actual R SDK callback/recording, connection handshake, Action 2 DUML and new controller-worker regressions passed.
- Management NVS/OTA fault-injection suite passed, including the BOOT busy guard.
- **17 Python manager tests** and **one hidden Tk GUI regression** passed. Serial/RTOS/BLE/NVS/OTA/LVGL substitutes do not prove physical behavior.
- Both ESP-IDF **v5.5.5** boards built in new `build-audit-c3` / `build-audit-c6` directories from the tracked defaults, without the existing sdkconfig or object caches. Existing toolchain and downloaded C6 LVGL 8.3.11 were reused. Final source changes were included in subsequent build checks.
- C3 app: **903,648 / 2,031,616 bytes**; C6 app: **1,290,688 / 4,128,768 bytes**. Both fit their dual-slot layouts with rollback enabled. These are image sizes, not runtime memory measurements.
- Generated local `rotorrec-c3-1.2.0-dev-audit-update.zip` / `rotorrec-c6-1.2.0-dev-audit-update.zip` in their audit build directories; reloaded manifests, SHA-256, board/chip/version, ELF fingerprints and app bytes match the built images. USB flasher arguments include bootloader, partition table, OTA metadata and app; their file ranges do not overwrite the NVS region. No hardware write or physical rollback was tested.
- Local documentation links and `git diff --check` passed. Changes and audit packages remain local; no GitHub push or release publication was performed.

| Artifact | SHA-256 of application | ELF fingerprint |
| --- | --- | --- |
| C3 audit | `4f686b8cf4b9bfab7c0f7c96208aefcdee6b48813cb27ef15aa67e8d8c435693` | `5e8641d5a6a6610cf76451ff4b3abe74d34aff6ba67df58c82a499a74928c79c` |
| C6 audit | `35f7a69e858ec183824ad8da48973dc7f237506db7ef8df833a1d4e38e02d35f` | `58aadc9235cdfb49d6d688a1817e8e252d689a9df7626e031d70ad9abc3fa622` |

### Remaining acceptance boundaries

The repaired paths require current-version hardware regression on both boards. In particular: actual USB migration and retained profiles, FC passthrough entry/exit, first pairing/approval, 20 record cycles and playable files, camera/ESP restarts, BLE/UART/RX loss, four-line OSD and physical LCD/LED output, saved settings/Link Pause, interrupted updates and early-boot rollback. GoPro model compatibility, runtime heap under load and stress timing remain unverified. Existing earlier UART/OSD and Action 2 evidence does not establish these new results. The initial DJI search retry, stale FC OSD text and unknown extra Action 2/GoPro telemetry limitations below remain intentional and are explained in the user guide.

## 2026-10-22 protocol integration verification

The earlier Action 2 hardware results below remain valid historical evidence, reaffirmed by the project owner. This change preserves its known command payloads and status fields; the new receiver and controller behavior have not been flashed or hardware-regressed.

- DJI host tests pass against the actual R SDK adapter: remembered/first pairing mode, failed version reply, normal/photo/unknown/pre-record state gating, actual-state confirmation after ACK, no confirmation, missing/error ACK, disconnect, stale data and tick wrap.
- Actual DJI connection logic passes mocked transport tests for ACK plus approval, approval without ACK (one consumption), rejection, short approval, offline disconnect and a nonblocking disconnect callback.
- Extracted Action 2 DUML passes an independent captured query-byte comparison, every split boundary, concatenated frames, damaged CRC/noise resynchronization, maximum frame length and overflow rejection.
- GoPro protocol/API-version/status/advertisement/Betaflight tests pass. Optional identification-timeout handling and the full application reconnect worker are code/build checked, not simulated end-to-end; BLE compatibility still requires hardware.
- Betaflight passes 11,956 checks; management fault injection and 15 Python manager tests pass.
- ESP-IDF v5.5.5 final builds pass: C3 application 903,312 bytes; C6 application 1,290,240 bytes. No hardware was flashed and no new update ZIP was generated; older local ZIPs do not contain these changes.
- Nano source/command research is complete for this iteration, but no Nano runtime adapter is enabled. Neither the official support table nor the release notes checked provide current public confirmation of Nano R SDK support. Source dates, community alternatives and uncertainty are recorded in [protocol research](camera-protocol-research.md).

## 1.2 development software verification

On 2026-10-02:

- Implemented first-batch CamLink-inspired settings, two/four-line OSD, per-field DJI telemetry validity, optional low-battery warning and persistent Link Pause. Usage and physical acceptance are in [camera settings](camera-settings.md).
- ESP-IDF v5.5.5 builds pass for both unified board images. C3 app: 903,616 / 2,031,616-byte slot (56% free); C6 app: 1,290,528 / 4,128,768-byte slot (69% free). These are image/partition figures, not runtime heap measurements.
- Betaflight host suite passes 11,956 checks, including wire formatting, four-line boundaries, unknown/zero/stale fields, warning hysteresis and maximum thresholds, settings validation, paused OSD and LED brightness. This count includes loops and is not a count of independent scenarios.
- Actual DJI R SDK adapter callbacks pass native tests for video/photo/unknown modes, seconds/MB conversion, parameter validity, malformed/maximum-length extension fields, stale/disconnected data, command invalidation, UINT32_MAX sentinels and tick wrap. Transport, clock and locks are substituted; no BLE radio is exercised.
- GoPro packet/status/advertisement and Betaflight integration suite passes.
- Actual management receiver passes native NVS/OTA fault injection, including new defaults/invalid storage, immutable current vs saved settings, commit failure, replayed saves, paused pairing rejection, BOOT resume and offline recovery with stale recording/saving flags. Storage/OTA are simulated; physical power-cut behavior remains unverified.
- Fifteen Python manager tests pass for wire/settings/package/reboot handling and existing update retries. One hidden Tk regression passes using ESP-IDF Python 3.12, Tk and pyserial: connection fields stay locked through queued replies; successful status populates settings; changing connections requires a fresh read; worker exceptions restore controls. No real serial device is opened. The system Python lacks pyserial; the GUI regression used the existing configured runtime.
- Versioned update ZIPs were generated for C3/C6, archive contents/hash/board/version were read back and compared with the final binaries. No hardware was flashed.

### Standards

Review covered the uncommitted implementation against baseline `83c3bf1`, repository architecture and the code-review smell baseline. One confirmed issue: GUI connection edits during a worker could apply an old status reply to a new connection. Connection/settings controls now remain locked until the FIFO completion event; the hidden Tk regression covers success and exception restoration. No remaining confirmed standards defects were found; no hard architecture violations were reported.

### Spec

Review used the first-batch scope in [CamLink evaluation](camlink-evaluation.md). One confirmed issue: stale recording/saving flags after a complete disconnect could block Link Pause/reboot. The busy gate now requires a live connection for those flags, still rejects pending commands and live unknown/recording/saving states, and passes offline-recovery regression. No remaining confirmed spec defects were found. Optional Wi-Fi/ARM/new-model features remain deferred.

Review totals: Standards 1 finding resolved, worst was the GUI connection race; Spec 1 finding resolved, worst was blocked offline recovery. New features still require both-board BLE/OSD/power-cycle/update acceptance; the earlier owner's UART/OSD confirmation does not establish 1.2 validation.

## 1.1 software verification

- Unified C3-Zero and C6-LCD builds pass with two OTA slots and rollback enabled. C3 app: 900,528 bytes / 2,031,616-byte slot; C6 app: 1,287,296 bytes / 4,128,768-byte slot.
- Link-time RAM usage: C3 124,506 / 321,296 bytes; C6 199,520 / 452,112 bytes. These are static linker figures, not free heap measured during BLE operation.
- Matching update ZIPs and segmented USB-install ZIPs were generated in each `build-passthrough-*` directory and checked for archive integrity; update manifests/hashes were read back and matched to the final app binaries.
- Tk GUI construction passes a hidden-window smoke check using the existing ESP-IDF Python runtime (pyserial 3.5, Tk 8.6); interactive device operation is not covered.
- Existing Betaflight host suite: 10,971 checks pass; GoPro protocol/integration host suite passes.
- Actual management receiver tested with NVS/OTA fault injection and native SHA-256; framing includes one million noise bytes and a CRC32 known vector.
- Ten Python manager checks pass, including full mocked update/reboot with a dropped DATA ACK, same-version rollback detection, corrupted package rejection and cancellation.
- These are software tests. Real passthrough, runtime camera switching, GUI interaction, Flash power-cut recovery, RAM headroom under BLE load and bootloader rollback remain **pending on both boards**. See [passthrough acceptance](passthrough.md).

## Hardware evidence (earlier firmware)

| Area | Confirmed | Remaining checks |
| --- | --- | --- |
| Action 2 | Initial pairing, start/stop, actual recording state, battery, Bluetooth icon, and keepalive | Long-duration and repeated regression |
| Action 2 reconnection | Individual camera/module restarts and recovery after going out of range during recording; recording can be stopped after recovery | Ten restarts per device, 20 repeated recording cycles, recording-file integrity |
| Action 4 | Pairing, recording control, and settings retrieval in an earlier version | Full current modular-version regression |
| C6 LCD | Orientation, offset, buffer, and SPI initialization tested previously | Current-version hardware regression |
| C3-Zero | Flash verification, Action 2 session, standby and battery reception; UART TX0/RX1 confirmed in logs | Visual confirmation of corrected RGB colors; full button and reconnection regression |
| Betaflight UART and OSD | Project owner reports successful real-hardware testing and use; host tests also cover MSPv2, USER control, OSD, and loss-of-link policy | Repeated stress and fault-injection coverage beyond the reported functional test |
| GoPro | Software implementation and host protocol tests only | Pairing, start/stop, battery, keepalive, reboot/range recovery, BOOT and BF integration on a specific model/firmware |

## Known limitations

- When DJI is selected, without a saved camera, an unsuccessful initial search requires a BOOT hold or RESET. Saved targets reconnect automatically. GoPro repeats discovery automatically.
- Action 2 elapsed recording time is estimated only after a confirmed OFF-to-ON transition and marked `~`; reconnecting to an already recording camera leaves it unknown. Remaining capacity is unavailable. Unverified fields are not treated as valid data.
- Action 2 pairing retains fixed fields from an existing remote flow; behavior across multiple cameras has not been established.
- C3 uses RGB byte order based on observed color reversal. Other boards may differ; host tests cannot verify visible color.
- The FC may retain the last OSD text after module power loss or UART disconnection. See [Betaflight setup](betaflight-setup.md).
- GoPro uses saved runtime protocol selection and requires Video mode. No GoPro model is hardware-verified yet. See [GoPro setup](gopro.md) for protocol scope and acceptance checks.

## Initial release software checks

On 2026-09-18, the tracked source was copied to a separate clean directory without local sdkconfig files, downloaded components, or existing build caches:

- MSVC host tests passed 10,971 checks; this is not a count of independent scenarios.
- ESP-IDF v5.5.5 builds passed for ESP32-C3 and ESP32-C6.
- Both build descriptions reported project_name=rotorrec and project_version=1.0.0.
- Initial C3 application: 864,960 bytes, 44% partition space free. Initial C6 application: 1,257,120 bytes, 18% free. These sizes describe the original release before the English string update.
- The automated build session did not flash hardware. The owner's UART/OSD hardware confirmation is recorded separately above.

Acceptance procedures are in [requirements](requirements.md); build instructions are in [building and flashing](build-and-flash.md). Historical raw records and binary snapshots remain local and are excluded from Git.

## GoPro integration software checks

On 2026-09-19:

- GoPro host tests passed: packet reassembly, malformed/truncated replies, continuation order/wrap, status validation, advertisement filtering and shared Betaflight policy/OSD.
- The existing Betaflight suite passed all 10,971 checks.
- ESP-IDF v5.5.5 builds passed for all four board/protocol combinations: C3 GoPro, C6 GoPro, C3 DJI and C6 DJI.
- GoPro application sizes: C3 804,864 bytes (48% partition space free); C6 1,198,304 bytes (22% free).
- No hardware was flashed or tested during this integration. BLE pairing, radio timing and actual recording remain pending hardware acceptance.

## English documentation and text update

On 2026-09-18, all tracked files passed a scan for Chinese characters and all local Markdown links resolved. A source-token comparison confirmed that C/C++ code changed only in comments and explicitly translated display/log strings. The 10,971 host checks and both C3/C6 builds passed again. Firmware version remains 1.0.0; no hardware was flashed during this text update.
