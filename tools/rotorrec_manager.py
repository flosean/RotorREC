"""RotorREC serial management. Only pyserial is required; GUI uses stdlib tkinter."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import secrets
import struct
import time
import zipfile
import zlib

MAGIC = b"RREC"
HEADER = struct.Struct("<4sBBIIH")
HELLO, INFO, SET_CAMERA, PAIR, BEGIN, DATA, END, ABORT, REBOOT, EXIT = range(1, 11)
SET_SETTINGS = 11
TELEMETRY = 12
OSD_LAYOUT, SET_OSD_LAYOUT = 13, 14
RESET = 18

def encode_layout(rows):
    if not isinstance(rows, list) or len(rows) != 4:
        raise ValueError("OSD needs four rows")
    wire = bytearray(257)
    wire[0] = 1
    for i, row in enumerate(rows):
        if not isinstance(row, str) or len(row) > 63 or any(not 32 <= ord(c) <= 126 for c in row):
            raise ValueError("Each OSD row allows 63 ASCII characters and displays 16 cells")
        tokens = re.findall(r"\{(?:state|battery|elapsed|remaining|format|space|mode)\}", row)
        literal = re.sub(r"\{(?:state|battery|elapsed|remaining|format|space|mode)\}", "", row)
        if "{" in literal or "}" in literal or len(tokens) > 8:
            raise ValueError("Unknown OSD token or more than eight tokens in one row")
        wire[1 + i * 64:1 + i * 64 + len(row)] = row.encode("ascii")
    return bytes(wire)

def decode_layout(wire):
    if len(wire) != 257 or wire[0] != 1:
        raise ValueError("Invalid OSD layout version / length")
    rows = []
    for i in range(4):
        row = wire[1 + i * 64:65 + i * 64]
        if b"\0" not in row:
            raise ValueError("Unterminated OSD row")
        rows.append(row.split(b"\0", 1)[0].decode("ascii"))
    encode_layout(rows)
    return rows
CAMERAS = {"action2": 2, "dji-rsdk": 1, "gopro": 3}
BOARDS = {"rotorrec_esp32c3": 5, "rotorrec_esp32c6": 13}
RECORD_MODES = {"USER": 0, "ARM": 1, "Button": 2, "Combi": 3, "Off": 4}
CONTROL_KEYS = {"record_mode", "stop_delay_seconds", "auto_record", "auto_stop", "low_remaining_seconds", "heads_up_warnings"}
MAX_PAYLOAD = 1040
PHASES = ("Starting", "Idle", "Searching", "Waiting for approval", "Connecting Action 2",
          "Approve pairing on the camera", "Waiting for camera status", "Connected",
          "Reconnecting", "Camera not found", "Unsupported camera services",
          "Pairing rejected", "Status subscription failed", "Link Pause — hold BOOT to resume")


def decode_settings(raw):
    if type(raw) is not int or raw < 0 or raw > 0xFFFFFFFF or raw & 0xFFF80000 != 0x01000000:
        raise ValueError("Unsupported or invalid settings format")
    value = dict(user_mode_id=40 + (raw & 3), osd_lines=4 if raw & 4 else 2,
                 link_paused=bool(raw & 8), led_percent=(raw >> 4) & 127,
                 low_battery_percent=(raw >> 11) & 127, owns_extra_lines=bool(raw & (1 << 18)))
    encode_settings(value)
    return value


def encode_settings(value):
    integers = ("user_mode_id", "osd_lines", "led_percent", "low_battery_percent")
    if any(type(value.get(key)) is not int for key in integers) or \
       type(value.get("link_paused")) is not bool or type(value.get("owns_extra_lines")) is not bool or \
       not 40 <= value["user_mode_id"] <= 43 or value["osd_lines"] not in (2, 4) or \
       not 1 <= value["led_percent"] <= 100 or not 0 <= value["low_battery_percent"] <= 100 or \
       (value["osd_lines"] == 4 and not value["owns_extra_lines"]):
        raise ValueError("Invalid USER, OSD, LED, battery threshold or pause setting")
    return (0x01000000 | (value["user_mode_id"] - 40) | (4 if value["osd_lines"] == 4 else 0) |
            (8 if value["link_paused"] else 0) | (value["led_percent"] << 4) |
            (value["low_battery_percent"] << 11) | (int(value["owns_extra_lines"]) << 18))


def encode_control(value):
    ranges = {"record_mode": (0, 4), "stop_delay_seconds": (0, 30), "low_remaining_seconds": (0, 3600)}
    if any(type(value.get(key)) is not int or not low <= value[key] <= high for key, (low, high) in ranges.items()) or \
       any(type(value.get(key)) is not bool for key in ("auto_record", "auto_stop", "heads_up_warnings")):
        raise ValueError("Invalid recording mode, stop delay or warning threshold")
    return (0x01000000 | value["record_mode"] | (value["stop_delay_seconds"] << 3) |
            (int(value["auto_record"]) << 8) | (int(value["auto_stop"]) << 9) |
            (value["low_remaining_seconds"] << 10) | (int(value["heads_up_warnings"]) << 22))


def decode_control(raw):
    if type(raw) is not int or not 0 <= raw <= 0xFFFFFFFF or raw & 0xFF800000 != 0x01000000:
        raise ValueError("Unsupported recording settings format")
    value = dict(record_mode=raw & 7, stop_delay_seconds=(raw >> 3) & 31,
                 auto_record=bool(raw & (1 << 8)), auto_stop=bool(raw & (1 << 9)),
                 low_remaining_seconds=(raw >> 10) & 4095, heads_up_warnings=bool(raw & (1 << 22)))
    encode_control(value)
    return value


def describe_status(value):
    phase = value.get("phase", 0)
    camera = {1: "DJI R SDK", 2: "DJI Action 2", 3: "GoPro"}.get(value.get("camera"), "Unknown")
    recording = "Recording" if value.get("recording") else "Standby"
    if value.get("command_pending"):
        recording = "Waiting for camera command"
    elif value.get("saving"):
        recording = "Camera busy / saving"
    elif not value.get("recording_valid"):
        recording = "Recording status unavailable"
    feedback = {1: "Waiting for recording start", 2: "Waiting for recording stop",
                3: "Recording start failed", 4: "Camera has not stopped recording",
                5: "Recording state unknown — check camera",
                6: "Recording stopped unexpectedly — check camera"}
    recording = feedback.get(value.get("record_result"), recording)
    if not value.get("record_result") and value.get("stop_delay_remaining"):
        recording = f"Stop requested in {value['stop_delay_remaining']} seconds; wait for camera confirmation"
    battery = f"{value['battery']}%" if value.get("battery_valid") else "Unavailable"
    result = (f"RotorREC {value.get('version', '?')} | {camera}\n"
              f"{PHASES[phase] if 0 <= phase < len(PHASES) else 'Unknown state'} | {recording} | Battery: {battery}")
    if phase in (3, 5) and value.get("camera") != CAMERAS["gopro"]:
        result += f"\nPairing code: {value.get('pairing_code', 0):04d} — check the camera screen."
    if value.get("pending_verify"):
        result += "\nChecking the new firmware…"
    if value.get("storage_error"):
        result += "\nSettings storage needs recovery; camera changes are unavailable."
    if value.get("stop_storage_error"):
        result += "\nPending STOP could not be saved; check the camera before restart or power off."
    if "telemetry" in value:
        telemetry = value["telemetry"]
        def duration(seconds):
            return "Unknown" if seconds is None else f"{seconds // 60}:{seconds % 60:02d}"
        result += (f"\nREC {'~' if telemetry.get('record_time_estimated') else ''}{duration(telemetry.get('record_time'))}"
                   f" | Remaining {duration(telemetry.get('remain_time'))}"
                   f" | SD {str(telemetry['remain_capacity']) + ' MB' if telemetry.get('remain_capacity') is not None else 'Unknown'}"
                   f" | {telemetry.get('parameters') or 'Format unknown'}")
        warnings = {1: "No SD card", 2: "SD full", 4: "SD format error", 8: "SD write speed error",
                    16: "Select video mode", 32: "Camera hot", 64: "Camera cold",
                    128: "Low remaining recording time", 256: "Low camera battery"}
        active = [message for bit, message in warnings.items() if telemetry.get("warnings", 0) & bit]
        if active:
            result += "\nWARNING: " + "; ".join(active)
    if "settings_raw" in value:
        settings = decode_settings(value["settings_raw"])
        result += (f"\nUSER{settings['user_mode_id'] - 39} | {settings['osd_lines']} OSD lines | "
                   f"LED {settings['led_percent']}% | Low battery: "
                   f"{str(settings['low_battery_percent']) + '%' if settings['low_battery_percent'] else 'Off'}")
        if value.get("saved_settings_raw", value["settings_raw"]) != value["settings_raw"]:
            result += "\nSaved settings are waiting for restart."
    if "control_raw" in value:
        control = decode_control(value["control_raw"])
        name = next(name for name, number in RECORD_MODES.items() if number == control["record_mode"])
        result += f"\nRecording: {name} | Stop delay: {control['stop_delay_seconds']} s"
        if value.get("saved_control_raw", value["control_raw"]) != value["control_raw"]:
            result += "\nSaved recording settings are waiting for restart."
    return result


def encode(command, session, sequence, payload=b""):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("Packet too large")
    frame = HEADER.pack(MAGIC, 1, command, session, sequence, len(payload)) + payload
    return frame + struct.pack("<I", zlib.crc32(frame))


class Parser:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        packets = []
        while self.buffer:
            if not MAGIC.startswith(self.buffer[:min(4, len(self.buffer))]):
                del self.buffer[0]
                continue
            if len(self.buffer) < HEADER.size:
                break
            _, version, command, session, sequence, length = HEADER.unpack_from(self.buffer)
            if version != 1 or length > MAX_PAYLOAD:
                del self.buffer[0]
                continue
            size = HEADER.size + length + 4
            if len(self.buffer) < size:
                break
            if zlib.crc32(self.buffer[:size - 4]) != struct.unpack_from("<I", self.buffer, size - 4)[0]:
                del self.buffer[0]
                continue
            packets.append((command, session, sequence, bytes(self.buffer[16:size - 4])))
            del self.buffer[:size]
        return packets


def image_metadata(image):
    if len(image) < 288 or image[0] != 0xE9 or struct.unpack_from("<I", image, 32)[0] != 0xABCD5432:
        raise ValueError("Expected an ESP application image, not a merged factory image")
    def field(offset):
        value = image[offset:offset + 32]
        if b"\0" not in value:
            raise ValueError("Invalid application descriptor")
        return value.split(b"\0", 1)[0].decode("ascii")
    board, version = field(80), field(48)
    if board not in BOARDS or not re.fullmatch(r"[A-Za-z0-9_.+\-]{1,31}", version) or struct.unpack_from("<H", image, 12)[0] != BOARDS[board]:
        raise ValueError("Image board/chip is not supported")
    return dict(product="RotorREC", board=board, version=version, protocol=1, layout=1,
                size=len(image), sha256=hashlib.sha256(image).hexdigest(),
                elf_sha256=image[176:208].hex())


def make_package(binary, output):
    image = Path(binary).read_bytes()
    metadata = image_metadata(image)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as package:
        package.writestr("manifest.json", json.dumps(metadata, indent=2))
        package.writestr("app.bin", image)
    return metadata


def load_package(path):
    with zipfile.ZipFile(path) as package:
        if sorted(package.namelist()) != ["app.bin", "manifest.json"]:
            raise ValueError("Package must contain only app.bin and manifest.json")
        if package.getinfo("app.bin").file_size > 0x3F0000 or package.getinfo("manifest.json").file_size > 4096:
            raise ValueError("Package exceeds supported size")
        metadata = json.loads(package.read("manifest.json"))
        image = package.read("app.bin")
    if metadata != image_metadata(image):
        raise ValueError("Manifest does not match the application image")
    return metadata, image


class DeviceError(RuntimeError):
    pass


class Client:
    def __init__(self, port):
        self.port = port
        self.parser = Parser()
        self.session = secrets.randbelow(0xFFFFFFFF) + 1
        self.sequence = 0

    def request(self, command, payload=b"", timeout=5, retries=3):
        wire = encode(command, self.session, self.sequence, payload)
        for _ in range(retries):
            self.port.write(wire)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                for cmd, session, sequence, data in self.parser.feed(self.port.read(2048)):
                    if (cmd, session, sequence) != (command | 0x80, self.session, self.sequence):
                        continue
                    if len(data) < 4:
                        raise ValueError("Short device response")
                    self.sequence += 1
                    error = struct.unpack_from("<I", data)[0]
                    if error:
                        raise DeviceError(f"Device rejected command {command}: 0x{error:x}")
                    return data[4:]
            self.parser = Parser()  # Drop a truncated reply before retrying the identical request.
        raise TimeoutError("No response. Check power/UART; a previous session expires after 30 seconds.")

    def info(self, hello=False, **request_options):
        value = json.loads(self.request(HELLO if hello else INFO, **request_options))
        if value.get("product") != "RotorREC" or value.get("protocol") != 1:
            raise ValueError("Unsupported device")
        if value.get("telemetry_version") == 1:
            value["telemetry"] = json.loads(self.request(TELEMETRY, **request_options))
        if value.get("osd_layout_version") == 1:
            layouts = self.request(OSD_LAYOUT, **request_options)
            if len(layouts) != 514:
                raise ValueError("Incomplete OSD layout reply")
            value["osd_layout"], value["saved_osd_layout"] = decode_layout(layouts[:257]), decode_layout(layouts[257:])
        return value

    def set_layout(self, rows):
        wire = encode_layout(rows)
        if self.info().get("osd_layout_version") != 1:
            raise ValueError("Update firmware to edit the OSD layout")
        self.request(SET_OSD_LAYOUT, wire)
        if self.info().get("saved_osd_layout") != rows:
            raise ValueError("Saved OSD layout did not match")
        value = self.reboot()
        if value.get("osd_layout") != rows:
            raise ValueError("OSD layout did not match after reboot")
        return value

    def reboot(self, version=None, camera=None, elf_sha256=None, settings_raw=None, control_raw=None):
        try:
            self.request(REBOOT, timeout=1, retries=1)
        except TimeoutError:
            pass  # The ESP may reboot before the reply reaches the host.
        return self.wait_for_boot(version, camera, elf_sha256, settings_raw, control_raw)

    def wait_for_boot(self, version=None, camera=None, elf_sha256=None, settings_raw=None, control_raw=None):
        # A lost END ACK is also recoverable: the receiver reboots on session expiry.
        # Only HELLO/INFO here, so we never extend the old committed session.
        deadline = time.monotonic() + 40
        self.session = secrets.randbelow(0xFFFFFFFF) + 1
        self.sequence = 0
        self.parser = Parser()
        while time.monotonic() < deadline:
            try:
                value = self.info(hello=self.sequence == 0,
                                  timeout=min(3, max(.1, deadline - time.monotonic())), retries=1)
                if value.get("pending_verify"):
                    time.sleep(.5)
                    continue
                if version is not None and value["version"] != version:
                    raise RuntimeError("Version verification failed; device may have rolled back")
                if camera is not None and value["camera"] != camera:
                    raise RuntimeError("Camera selection did not persist")
                if elf_sha256 is not None and value.get("elf_sha256") != elf_sha256:
                    raise RuntimeError("Device booted a different image (possibly rollback)")
                if settings_raw is not None and value.get("settings_raw") != settings_raw:
                    raise RuntimeError("Settings did not persist or the device booted older firmware")
                if control_raw is not None and value.get("control_raw") != control_raw:
                    raise RuntimeError("Recording settings did not persist after reboot")
                return value
            except TimeoutError:
                # A pending image can roll back after accepting our HELLO.
                if self.sequence:
                    self.session = secrets.randbelow(0xFFFFFFFF) + 1
                    self.sequence = 0
                    self.parser = Parser()
                time.sleep(.2)
        raise TimeoutError("Reboot could not be verified; reconnect and inspect device status")

    def reset_device(self, scope):
        if type(scope) is not int or scope not in (1, 2):
            raise ValueError("Reset scope must be 1 (pairings) or 2 (factory)")
        current = self.info()
        if current.get("reset_capable") != 1:
            raise ValueError("Update firmware before resetting this device")
        if current.get("wifi_active"):
            raise ValueError("Use the Wi-Fi web page to reset a device in Wi-Fi mode")
        self.request(RESET, bytes([scope]))
        if self.info().get("reset_pending") != scope:
            raise RuntimeError("Reset request was not saved")
        value = self.reboot()
        if value.get("reset_pending") != 0 or value.get("storage_error") != 0:
            raise RuntimeError("Reset did not finish; reconnect and inspect storage status")
        if scope == 1 and not decode_settings(value["settings_raw"])["link_paused"]:
            raise RuntimeError("Pairing removal did not enable Link Pause")
        return value

    def set_settings(self, **changes):
        if set(changes) - ({"user_mode_id", "osd_lines", "led_percent", "low_battery_percent", "link_paused"} | CONTROL_KEYS):
            raise ValueError("Unknown setting")
        current = self.info()
        if "settings_raw" not in current:
            raise ValueError("This firmware does not support saved settings; update it first")
        value = decode_settings(current.get("saved_settings_raw", current["settings_raw"]))
        extended = "control_raw" in current
        if extended:
            value.update(decode_control(current.get("saved_control_raw", current["control_raw"])))
        elif set(changes) & CONTROL_KEYS:
            raise ValueError("Update firmware before changing recording controls")
        value.update(changes)
        value["owns_extra_lines"] |= value["osd_lines"] == 4
        raw = encode_settings(value)
        expected = dict(settings_raw=raw)
        payload = struct.pack("<I", raw)
        if extended:
            expected["control_raw"] = encode_control(value)
            payload += struct.pack("<I", expected["control_raw"])
        self.request(SET_SETTINGS, payload)
        return self.reboot(**expected)

    def update(self, metadata, image, progress=print, cancelled=lambda: False):
        current = self.info()
        if metadata["board"] != current["board"] or metadata["layout"] != current["layout"]:
            raise ValueError("Wrong board or partition layout")
        if len(image) > current["capacity"]:
            raise ValueError("Application does not fit the update partition")
        payload = struct.pack("<II32s32s32s", len(image), metadata["layout"],
                              metadata["board"].encode(), metadata["version"].encode(),
                              bytes.fromhex(metadata["sha256"]))
        commit_started = False
        try:
            self.request(BEGIN, payload)
            last_percent = -1
            for offset in range(0, len(image), 1024):
                if cancelled():
                    raise RuntimeError("Update cancelled; the current firmware remains selected")
                chunk = image[offset:offset + 1024]
                self.request(DATA, struct.pack("<I", offset) + chunk)
                percent = (offset + len(chunk)) * 100 // len(image)
                if percent != last_percent:
                    progress(f"Updating: {percent}%")
                    last_percent = percent
            if cancelled():
                raise RuntimeError("Update cancelled; the current firmware remains selected")
            # After END is sent, a missing reply cannot prove that commit failed.
            commit_started = True
            try:
                self.request(END)
            except TimeoutError:
                progress("Final acknowledgement lost; waiting for automatic restart and verifying the image.")
                return self.wait_for_boot(version=metadata["version"], elf_sha256=metadata["elf_sha256"])
            except DeviceError:
                commit_started = False
                raise
            return self.reboot(version=metadata["version"], elf_sha256=metadata["elf_sha256"])
        except Exception:
            if not commit_started:
                try:
                    self.request(ABORT, timeout=1, retries=1)
                except (TimeoutError, DeviceError):
                    pass
            raise


def cli_response(port, command, timeout=3):
    port.write(command + b"\r\n")
    data = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = port.read(2048)
        data.extend(chunk)
        # Comments also begin '# '. Require a full prompt and an idle read,
        # otherwise a USB packet boundary inside '# Betaflight' truncates it.
        if not chunk and data.endswith(b"\n# "):
            return data.decode("ascii", errors="replace")
    raise TimeoutError("Betaflight CLI did not respond. Close Configurator; if already in passthrough use --direct.")


def enter_passthrough(port, uart=None, log=print):
    port.write(b"#")
    time.sleep(.3)
    port.reset_input_buffer()
    version = cli_response(port, b"version")
    if "Betaflight" not in version:
        raise ValueError("This serial port did not identify itself as Betaflight")
    log("Betaflight identified; checking the selected UART.")
    ports = cli_response(port, b"serial")
    candidates = []
    for identifier, functions, baud in re.findall(r"^serial (\d+) (\d+) (\d+) ", ports, re.M):
        if 0 <= int(identifier) < 20 and int(functions) & 1:
            candidates.append((int(identifier) + 1, int(baud)))
    if uart is None:
        if len(candidates) != 1:
            raise ValueError(f"Select the UART wired to RotorREC with --uart. MSP candidates: {candidates}")
        uart = candidates[0][0]
    if (uart, 115200) not in candidates:
        raise ValueError("Selected UART must already have MSP enabled at 115200 in Betaflight")
    port.write(f"serialpassthrough {uart - 1} 115200 rxtx\r\n".encode())
    deadline = time.monotonic() + 3
    data = bytearray()
    while time.monotonic() < deadline:
        data.extend(port.read(2048))
        if b"Forwarding" in data:
            return
    raise TimeoutError("Betaflight did not confirm passthrough")


def run_device(args, log=print, entered=lambda: None, cancelled=lambda: False):
    import serial
    package = load_package(args.file) if args.action == "update" else None
    port = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=3)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    client = Client(port)
    connected = False
    try:
        if not args.direct:
            enter_passthrough(port, args.uart, log)
        entered()
        value = client.info(hello=True)
        connected = True
        log(json.dumps(value, indent=2))
        if args.action == "camera":
            choice = CAMERAS[args.camera]
            client.request(SET_CAMERA, bytes([choice]))
            value = client.reboot(camera=choice)
        elif args.action == "pair":
            client.request(PAIR)
            # DJI may spend 30s scanning, 30s discovering GATT and 30s awaiting approval.
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                time.sleep(1)
                value = client.info()
                log(json.dumps(value))
                if value["phase"] == 7 and not value["command_pending"]:  # CAMERA_PHASE_READY
                    break
            else:
                raise TimeoutError("Pairing not confirmed; check the camera approval screen and device status")
        elif args.action == "settings":
            changes = {key: getattr(args, key) for key in
                       ("osd_lines", "led_percent", "low_battery_percent", *sorted(CONTROL_KEYS))
                       if getattr(args, key, None) is not None}
            if args.user is not None:
                changes["user_mode_id"] = args.user + 39
            if isinstance(changes.get("record_mode"), str):
                changes["record_mode"] = RECORD_MODES[changes["record_mode"]]
            if not changes:
                raise ValueError("Choose at least one setting")
            value = client.set_settings(**changes)
        elif args.action == "link-pause":
            value = client.set_settings(link_paused=args.pause == "on")
        elif args.action == "osd":
            rows = args.rows if hasattr(args, "rows") else json.loads(Path(args.file).read_text(encoding="utf-8"))
            value = client.set_layout(rows)
        elif args.action == "reset":
            if not args.confirm:
                raise ValueError("Reset deletes pairing data; pass --confirm to proceed")
            value = client.reset_device(1 if args.scope == "pairings" else 2)
        elif args.action == "update":
            value = client.update(*package, progress=log, cancelled=cancelled)
        log(json.dumps(value, indent=2))
        log("Completed. Power-cycle the flight controller to leave passthrough and restore normal control.")
    finally:
        if connected:
            try:
                client.request(EXIT, timeout=.3, retries=1)
            except (OSError, TimeoutError, DeviceError):
                pass
        port.close()


def gui():
    import queue
    import threading
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from serial.tools import list_ports
    root = tk.Tk()
    root.title("RotorREC — camera & firmware")
    root.geometry("880x760")
    frame = ttk.Frame(root, padding=12)
    frame.pack(fill="both", expand=True)
    ttk.Label(frame, text="Close Betaflight Configurator. Power the camera controller. Use only on the bench.").pack(anchor="w")
    row = ttk.Frame(frame); row.pack(fill="x", pady=8)
    ttk.Label(row, text="FC port").pack(side="left")
    port = ttk.Combobox(row, values=[p.device for p in list_ports.comports()], width=14)
    port.pack(side="left", padx=6)
    ttk.Label(row, text="UART (blank = detect)").pack(side="left")
    uart = ttk.Entry(row, width=4); uart.pack(side="left", padx=6)
    direct = tk.BooleanVar()
    direct_toggle = ttk.Checkbutton(frame, text="FC is already in passthrough (reconnect without power cycling)", variable=direct)
    direct_toggle.pack(anchor="w")
    camera = ttk.Combobox(frame, values=list(CAMERAS), state="readonly")
    camera.set("action2"); camera.pack(anchor="w", pady=8)
    ttk.Label(frame, text="GoPro hardware validation is pending. Camera changes restart the ESP.").pack(anchor="w")
    settings_row = ttk.Frame(frame); settings_row.pack(fill="x", pady=6)
    fields = {}
    for label, key, choices, initial in [("USER", "user", (1, 2, 3, 4), 1),
                                       ("OSD lines", "osd_lines", (2, 4), 2),
                                       ("LED %", "led_percent", (5, 25, 100), 5),
                                       ("Low battery % (0=off)", "low_battery_percent", (0, 10, 20, 30), 0)]:
        ttk.Label(settings_row, text=label).pack(side="left", padx=3)
        field = ttk.Combobox(settings_row, values=choices, width=4,
                            state="readonly" if key in ("user", "osd_lines") else "normal")
        field.set(initial); field.pack(side="left", padx=3)
        fields[key] = field
    control_row = ttk.Frame(frame); control_row.pack(fill="x", pady=6)
    for label, key, choices, initial in [
        ("Recording", "record_mode", tuple(RECORD_MODES), "USER"),
        ("Stop delay (s)", "stop_delay_seconds", (0, 3, 5, 10, 30), 5),
        ("Low remaining (s)", "low_remaining_seconds", (0, 30, 60, 120), 0)]:
        ttk.Label(control_row, text=label).pack(side="left", padx=3)
        field = ttk.Combobox(control_row, values=choices, width=8,
                            state="readonly" if key == "record_mode" else "normal")
        field.set(initial); field.pack(side="left", padx=3); fields[key] = field
    flags_row = ttk.Frame(frame); flags_row.pack(fill="x", pady=6)
    for label, key in [("Record on ARM", "auto_record"), ("Stop on disarm", "auto_stop"),
                       ("Heads-up warnings", "heads_up_warnings")]:
        ttk.Label(flags_row, text=label).pack(side="left", padx=3)
        field = ttk.Combobox(flags_row, values=("On", "Off"), width=5, state="readonly")
        field.set("On"); field.pack(side="left", padx=3); fields[key] = field
    settings_ready = False
    control_ready = False
    last_info = {}
    def forget_settings(event=None):
        nonlocal settings_ready, control_ready, last_info
        settings_ready = False
        control_ready = False
        last_info = {}
        direct.set(False)
        set_controls_busy(False)
    port.bind("<<ComboboxSelected>>", forget_settings)
    port.bind("<KeyRelease>", forget_settings)
    uart.bind("<KeyRelease>", forget_settings)
    ttk.Label(frame, text="Read status before saving. Settings / Link Pause restart the ESP; pairing is retained.").pack(anchor="w")
    buttons = ttk.Frame(frame); buttons.pack(fill="x", pady=8)
    output = tk.Text(frame, wrap="word"); output.pack(fill="both", expand=True)
    events = queue.Queue()
    cancel = threading.Event()
    busy = False
    def set_controls_busy(value):
        # A reply belongs to the connection that started the worker. Keep that
        # connection immutable until all its queued replies have been consumed.
        port.configure(state="disabled" if value else "normal")
        uart.configure(state="disabled" if value else "normal")
        direct_toggle.configure(state="disabled" if value else "normal")
        camera.configure(state="disabled" if value else "readonly")
        for key, field in fields.items():
            field.configure(state="disabled" if value or (key in CONTROL_KEYS and not control_ready) else
                            "readonly" if key in ("user", "osd_lines", "record_mode", "auto_record", "auto_stop", "heads_up_warnings") else "normal")
    def start(action, layout_rows=None):
        nonlocal busy
        if busy:
            return
        try:
            selected_uart = int(uart.get()) if uart.get().strip() else None
            if selected_uart is not None and not 1 <= selected_uart <= 20:
                raise ValueError("UART must be between 1 and 20")
            if not port.get().strip():
                raise ValueError("Choose the flight-controller port")
            if action in ("settings", "pause", "resume") and not settings_ready:
                raise ValueError("Read status on this connection before changing settings")
            file = filedialog.askopenfilename(filetypes=[("RotorREC update", "*.zip")]) if action == "update" else None
            if action == "update" and not file:
                return
            args = argparse.Namespace(action=action, port=port.get(), uart=selected_uart,
                                      direct=direct.get(), camera=camera.get(), file=file)
            if action in ("reset-pairings", "reset-factory"):
                if last_info.get("reset_capable") != 1:
                    raise ValueError("Read status from firmware supporting reset first")
                factory = action == "reset-factory"
                message = "Delete all camera pairings" + (", settings, OSD layout and Wi-Fi credentials?" if factory else " and enable Link Pause? Other settings stay saved.")
                if not messagebox.askyesno("Confirm reset", message + "\nConfirm the camera has stopped recording.", parent=root):
                    return
                args.action = "reset"; args.scope = "factory" if factory else "pairings"; args.confirm = True
            if action == "osd":
                if last_info.get("osd_layout_version") != 1:
                    raise ValueError("Read status from firmware supporting OSD layouts first")
                encode_layout(layout_rows)
                args.rows = layout_rows
            if action == "settings":
                for key, field in fields.items():
                    if key in CONTROL_KEYS and not control_ready:
                        continue
                    selected = field.get()
                    parsed = RECORD_MODES[selected] if key == "record_mode" else \
                        selected == "On" if key in ("auto_record", "auto_stop", "heads_up_warnings") else int(selected)
                    setattr(args, key, parsed)
                if not 1 <= args.led_percent <= 100 or not 0 <= args.low_battery_percent <= 100:
                    raise ValueError("LED must be 1–100%; low battery must be 0–100%")
            elif action in ("pause", "resume"):
                args.action = "link-pause"
                args.pause = "on" if action == "pause" else "off"
        except ValueError as exc:
            output.insert("end", str(exc) + "\n"); return
        busy = True
        set_controls_busy(True)
        cancel.clear()
        def worker():
            try:
                run_device(args, events.put, lambda: events.put(("passthrough",)), cancel.is_set)
            except Exception as exc:
                events.put(f"FAILED: {exc}\nPower-cycle FC before a fresh connection, or use reconnect if still in passthrough.")
            finally:
                events.put(None)
        threading.Thread(target=worker, daemon=True).start()
    for label, action in [("Read status", "info"), ("Save camera & restart", "camera"),
                          ("Pair camera", "pair"), ("Update firmware", "update")]:
        ttk.Button(buttons, text=label, command=lambda a=action: start(a)).pack(side="left", padx=3)
    ttk.Button(buttons, text="Cancel update", command=cancel.set).pack(side="left", padx=3)
    settings_buttons = ttk.Frame(frame); settings_buttons.pack(fill="x", pady=6, before=output)
    for label, action in [("Save settings & restart", "settings"), ("Pause camera link", "pause"),
                          ("Resume camera link", "resume")]:
        ttk.Button(settings_buttons, text=label, command=lambda a=action: start(a)).pack(side="left", padx=3)
    def edit_layout():
        if busy or last_info.get("osd_layout_version") != 1:
            output.insert("end", "Read status from firmware supporting OSD layouts first.\n"); return
        popup = tk.Toplevel(root); popup.title("OSD layout — 16 visible cells per row")
        ttk.Label(popup, text="{state} {battery} {elapsed} {remaining} {format} {space} {mode}\nASCII text; blank keeps default. Errors always occupy row 1.").pack(padx=12, pady=8)
        entries = []
        for i, value in enumerate(last_info["saved_osd_layout"]):
            row = ttk.Frame(popup); row.pack(fill="x", padx=12, pady=3)
            ttk.Label(row, text=f"Row {i + 1}").pack(side="left")
            field = ttk.Entry(row, width=55); field.insert(0, value); field.pack(side="left"); entries.append(field)
        def save():
            rows = [field.get() for field in entries]
            try: encode_layout(rows)
            except ValueError as exc:
                output.insert("end", str(exc) + "\n"); return
            popup.destroy(); start("osd", rows)
        ttk.Button(popup, text="Save, restart and verify", command=save).pack(pady=8)
    def export_diagnostics():
        if busy or not last_info: return
        filename = filedialog.asksaveasfilename(defaultextension=".json", initialfile="rotorrec-diagnostics.json")
        if filename:
            Path(filename).write_text(json.dumps({"device": last_info, "log": output.get("1.0", "end"),
                "hardware_verified": False}, indent=2), encoding="utf-8")
    ttk.Button(settings_buttons, text="Edit OSD", command=edit_layout).pack(side="left", padx=3)
    ttk.Button(settings_buttons, text="Export diagnostics", command=export_diagnostics).pack(side="left", padx=3)
    reset_buttons = ttk.Frame(frame); reset_buttons.pack(fill="x", before=output, pady=4)
    ttk.Button(reset_buttons, text="Remove all camera pairings", command=lambda: start("reset-pairings")).pack(side="left", padx=3)
    ttk.Button(reset_buttons, text="Factory reset", command=lambda: start("reset-factory")).pack(side="left", padx=3)
    def poll():
        nonlocal busy, settings_ready, control_ready, last_info
        while not events.empty():
            item = events.get_nowait()
            if item == ("passthrough",):
                direct.set(True)
            elif item is None:
                busy = False
                set_controls_busy(False)
            else:
                if item.startswith("{"):
                    try:
                        value = json.loads(item)
                        last_info = value
                        if value.get("camera") in CAMERAS.values():
                            camera.set(next(name for name, ident in CAMERAS.items() if ident == value["camera"]))
                        if "settings_raw" in value:
                            saved = decode_settings(value.get("saved_settings_raw", value["settings_raw"]))
                            control_ready = "control_raw" in value
                            if control_ready:
                                saved.update(decode_control(value.get("saved_control_raw", value["control_raw"])))
                            for key, field in fields.items():
                                if key in CONTROL_KEYS and not control_ready:
                                    continue
                                selected = saved["user_mode_id"] - 39 if key == "user" else saved[key]
                                if key == "record_mode":
                                    selected = next(name for name, number in RECORD_MODES.items() if number == selected)
                                elif type(selected) is bool:
                                    selected = "On" if selected else "Off"
                                field.set(selected)
                            settings_ready = True
                        else:
                            settings_ready = False
                        item = describe_status(value)
                    except (ValueError, TypeError, KeyError):
                        pass
                output.insert("end", item + "\n"); output.see("end")
        root.after(100, poll)
    # Do not terminate a Flash transfer by accidentally closing the window.
    root.protocol("WM_DELETE_WINDOW", lambda: None if busy else root.destroy())
    set_controls_busy(False)
    poll(); root.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--uart", type=int, choices=range(1, 21))
    parser.add_argument("--direct", action="store_true", help="FC is already in passthrough")
    commands = parser.add_subparsers(dest="action", required=True)
    commands.add_parser("gui")
    commands.add_parser("info")
    commands.add_parser("pair")
    reset_command = commands.add_parser("reset", help="Remove all camera pairings or restore factory settings")
    reset_command.add_argument("scope", choices=("pairings", "factory"))
    reset_command.add_argument("--confirm", action="store_true", required=True)
    layout = commands.add_parser("osd", help="Save a JSON array of four OSD templates, restart and verify")
    layout.add_argument("file")
    settings = commands.add_parser("settings", help="Save selected settings and restart; unspecified values are retained")
    settings.add_argument("--user", type=int, choices=range(1, 5))
    settings.add_argument("--osd-lines", type=int, choices=(2, 4))
    settings.add_argument("--led-percent", type=int, choices=range(1, 101))
    settings.add_argument("--low-battery-percent", type=int, choices=range(0, 101))
    settings.add_argument("--record-mode", choices=RECORD_MODES)
    settings.add_argument("--stop-delay-seconds", type=int, choices=range(31))
    settings.add_argument("--low-remaining-seconds", type=int, choices=range(3601))
    for option in ("auto-record", "auto-stop", "heads-up-warnings"):
        settings.add_argument("--" + option, action=argparse.BooleanOptionalAction, default=None)
    pause = commands.add_parser("link-pause", help="Save Link Pause on/off and restart; retains pairing")
    pause.add_argument("pause", choices=("on", "off"))
    camera = commands.add_parser("camera"); camera.add_argument("camera", choices=CAMERAS)
    update = commands.add_parser("update"); update.add_argument("file")
    pack = commands.add_parser("pack"); pack.add_argument("binary"); pack.add_argument("output")
    args = parser.parse_args()
    if args.action == "gui":
        gui()
    elif args.action == "pack":
        print(json.dumps(make_package(args.binary, args.output), indent=2))
    else:
        if not args.port:
            parser.error("--port is required")
        run_device(args)


if __name__ == "__main__":
    try:
        main()
    except ModuleNotFoundError as error:
        raise SystemExit("Install pyserial (python -m pip install pyserial==3.5) and use Python with tkinter for the GUI.") from error
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        raise SystemExit(str(error))
