"""Wire compatibility, retry, package validation and host-side failure checks."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("manager", Path(__file__).parents[1] / "tools/rotorrec_manager.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def image():
    data = bytearray(1536)
    data[0] = 0xe9
    struct.pack_into("<H", data, 12, 5)
    struct.pack_into("<I", data, 32, 0xabcd5432)
    data[48:53] = b"1.1.0"
    data[80:112] = b"rotorrec_esp32c3".ljust(32, b"\0")
    data[176:208] = hashlib.sha256(b"elf").digest()
    return bytes(data)


class FakePort:
    def __init__(self, error=0, drop=False):
        self.requests = []
        self.output = b""
        self.error = error
        self.drop = drop

    def write(self, data):
        self.requests.append(data)
        cmd, session, sequence, payload = m.Parser().feed(data)[0]
        reply = m.encode(cmd | 0x80, session, sequence, struct.pack("<I", self.error) + b"ok")
        self.output += b"$X>noise" + (b"" if self.drop and len(self.requests) == 1 else reply)

    def read(self, size):
        out, self.output = self.output[:7], self.output[7:]
        return out


class UpdatePort:
    """A peripheral model; tests the real host client across reboot and ACK loss."""
    def __init__(self):
        self.output = b""
        self.data = bytearray()
        self.cache = {}
        self.committed = False
        self.aborted = False
        self.version = "1.0.0"
        self.elf = "old"
        self.lost_data_ack = False

    def write(self, wire):
        if wire in self.cache:
            self.output += self.cache[wire]
            return
        cmd, session, sequence, payload = m.Parser().feed(wire)[0]
        data = b""
        if cmd in (m.HELLO, m.INFO):
            data = json.dumps(dict(product="RotorREC", protocol=1, layout=1,
                board="rotorrec_esp32c3", capacity=4096, pending_verify=0,
                version=self.version, elf_sha256=self.elf)).encode()
        elif cmd == m.BEGIN:
            self.expected = struct.unpack("<II32s32s32s", payload)
        elif cmd == m.DATA:
            assert struct.unpack_from("<I", payload)[0] == len(self.data)
            self.data.extend(payload[4:])
        elif cmd == m.END:
            assert len(self.data) == self.expected[0]
            assert hashlib.sha256(self.data).digest() == self.expected[4]
            self.committed = True
        elif cmd == m.REBOOT:
            assert self.committed
            meta = m.image_metadata(self.data)
            self.version, self.elf = meta["version"], meta["elf_sha256"]
        elif cmd == m.ABORT:
            self.aborted = True
        reply = m.encode(cmd | 0x80, session, sequence, b"\0" * 4 + data)
        self.cache[wire] = reply
        if cmd == m.DATA and not self.lost_data_ack:
            self.lost_data_ack = True
        else:
            self.output += reply

    def read(self, size):
        data, self.output = self.output[:size], self.output[size:]
        return data


class Checks(unittest.TestCase):
    def test_settings_golden_and_validation(self):
        defaults = dict(user_mode_id=40, osd_lines=2, led_percent=5,
                        low_battery_percent=0, link_paused=False, owns_extra_lines=False)
        self.assertEqual(m.encode_settings(defaults), 0x01000050)
        self.assertEqual(m.decode_settings(0x01000050), defaults)
        for raw in (0, -1, 0x100000000, 0x02000050, 0x01080050, 0x01000000, 0x01000054, True):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                m.decode_settings(raw)
        for change in (dict(user_mode_id=39), dict(osd_lines=3), dict(led_percent=0),
                       dict(led_percent=101), dict(low_battery_percent=101), dict(link_paused=1),
                       dict(user_mode_id=True)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                m.encode_settings(defaults | change)
        for user in range(40, 44):
            for lines in (2, 4):
                for percent in (1, 5, 25, 100):
                    for threshold in (0, 20, 100):
                        for pause in (False, True):
                            value = defaults | dict(user_mode_id=user, osd_lines=lines,
                                owns_extra_lines=lines == 4, led_percent=percent,
                                low_battery_percent=threshold, link_paused=pause)
                            self.assertEqual(m.decode_settings(m.encode_settings(value)), value)

    def test_settings_merge_and_verified_reboot(self):
        client = m.Client(FakePort())
        initial = dict(user_mode_id=43, osd_lines=4, led_percent=25,
                       low_battery_percent=20, link_paused=False, owns_extra_lines=True)
        raw = m.encode_settings(initial)
        client.info = lambda: dict(settings_raw=0x01000050, saved_settings_raw=raw)
        observed = []
        client.reboot = lambda **kw: observed.append(kw) or kw
        result = client.set_settings(osd_lines=2, link_paused=True)
        expected = m.encode_settings(initial | dict(osd_lines=2, link_paused=True))
        self.assertEqual(result, dict(settings_raw=expected))
        command, _, _, payload = m.Parser().feed(client.port.requests[0])[0]
        self.assertEqual((command, struct.unpack("<I", payload)[0]), (m.SET_SETTINGS, expected))
        self.assertTrue(m.decode_settings(expected)["owns_extra_lines"])

    def test_settings_rejection_never_reboots(self):
        client = m.Client(FakePort(error=0x103))
        client.info = lambda: dict(settings_raw=0x01000050)
        client.reboot = lambda **kw: self.fail("Must not restart after rejecting settings")
        with self.assertRaises(m.DeviceError):
            client.set_settings(link_paused=True)
        client.port.requests.clear()
        client.info = lambda: dict(version="1.1.0")
        with self.assertRaisesRegex(ValueError, "update it first"):
            client.set_settings(osd_lines=4)
        self.assertEqual(client.port.requests, [])

    def test_reboot_detects_settings_loss(self):
        client = m.Client(FakePort())
        client.info = lambda hello=False: dict(pending_verify=0, settings_raw=0x01000050)
        with self.assertRaisesRegex(RuntimeError, "Settings did not persist"):
            client.reboot(settings_raw=0x01040054)

    def test_pause_status_never_claims_standby(self):
        value = dict(phase=13, settings_raw=0x01000058, saved_settings_raw=0x01000058,
                     link_paused=1, recording_valid=0)
        result = m.describe_status(value)
        self.assertIn("Link Pause", result)
        self.assertIn("Recording status unavailable", result)
        self.assertNotIn("Standby", result)

    def test_packets(self):
        wire = m.encode(m.DATA, 0x12345678, 19, bytes(range(256)) * 4)
        for split in range(len(wire) + 1):
            parser = m.Parser()
            replies = parser.feed(wire[:split]) + parser.feed(wire[split:])
            self.assertEqual(replies, [(m.DATA, 0x12345678, 19, bytes(range(256)) * 4)])
        damaged = bytearray(wire); damaged[-1] ^= 1
        invalid = m.HEADER.pack(m.MAGIC, 1, m.DATA, 1, 0, 65535)
        self.assertEqual(len(m.Parser().feed(b"garbage" + damaged + invalid + wire + wire)), 2)

    def test_retry_identical(self):
        port = FakePort(drop=True)
        client = m.Client(port)
        self.assertEqual(client.request(m.PAIR, timeout=.002), b"ok")
        self.assertEqual(len(port.requests), 2)
        self.assertEqual(port.requests[0], port.requests[1])
        self.assertEqual(client.sequence, 1)

    def test_rejected_command_advances_sequence(self):
        port = FakePort(error=0x103)
        client = m.Client(port)
        with self.assertRaises(m.DeviceError):
            client.request(m.BEGIN)
        self.assertEqual(client.sequence, 1)
        self.assertEqual(len(port.requests), 1)

    def test_package(self):
        data = image()
        with tempfile.TemporaryDirectory() as folder:
            binary, output = Path(folder) / "app.bin", Path(folder) / "update.zip"
            binary.write_bytes(data)
            metadata = m.make_package(binary, output)
            self.assertEqual(m.load_package(output), (metadata, data))
            metadata["board"] = "rotorrec_esp32c6"
            with zipfile.ZipFile(output, "w") as package:
                package.writestr("app.bin", data)
                package.writestr("manifest.json", json.dumps(metadata))
            with self.assertRaises(ValueError):
                m.load_package(output)

    def test_wrong_image(self):
        with self.assertRaises(ValueError):
            m.image_metadata(b"factory")
        data = bytearray(image()); data[12] = 13
        with self.assertRaises(ValueError):
            m.image_metadata(data)

    def test_no_upload_to_wrong_board(self):
        client = m.Client(FakePort())
        client.info = lambda: dict(board="rotorrec_esp32c6", layout=1, capacity=999999)
        with self.assertRaises(ValueError):
            client.update(m.image_metadata(image()), image())
        self.assertEqual(client.port.requests, [])

    def test_reboot_detects_same_version_rollback(self):
        client = m.Client(FakePort())
        client.info = lambda hello=False: dict(version="1.1.0", pending_verify=0, elf_sha256="old")
        with self.assertRaisesRegex(RuntimeError, "different image"):
            client.reboot(version="1.1.0", elf_sha256="new")

    def test_update_roundtrip_and_lost_ack(self):
        port = UpdatePort()
        client = m.Client(port)
        request = client.request
        client.request = lambda cmd, payload=b"", **kw: request(cmd, payload, timeout=.005)
        client.info(hello=True)
        result = client.update(m.image_metadata(image()), image(), progress=lambda _: None)
        self.assertEqual(result["version"], "1.1.0")
        self.assertTrue(port.committed)
        self.assertEqual(port.data, image())

    def test_cancel_never_commits(self):
        port = UpdatePort()
        client = m.Client(port)
        with self.assertRaisesRegex(RuntimeError, "cancelled"):
            client.update(m.image_metadata(image()), image(), cancelled=lambda: True)
        self.assertFalse(port.committed)
        self.assertTrue(port.aborted)
        self.assertEqual(port.data, b"")

    def test_cli_does_not_probe_other_peripherals(self):
        class CLI:
            def __init__(self):
                self.output = b""; self.commands = []
            def write(self, data):
                self.commands.append(data)
                if data == b"version\r\n": self.output = b"# Betaflight / STM32 2025.12\r\n# "
                elif data == b"serial\r\n": self.output = b"serial 0 1 115200 0 0 0\r\nserial 1 64 115200 0 0 0\r\n# "
                elif data.startswith(b"serialpassthrough"): self.output = b"Forwarding, power cycle to exit"
            def read(self, size):
                data, self.output = self.output, b""; return data
            def reset_input_buffer(self): pass
        port = CLI()
        m.enter_passthrough(port, 1, log=lambda _: None)
        self.assertIn(b"serialpassthrough 0 115200 rxtx\r\n", port.commands)
        port = CLI()
        with self.assertRaises(ValueError):
            m.enter_passthrough(port, 2, log=lambda _: None)
        self.assertFalse(any(cmd.startswith(b"serialpassthrough") for cmd in port.commands))


if __name__ == "__main__":
    unittest.main()
