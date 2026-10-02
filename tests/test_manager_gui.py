"""Hidden Tk regression: connection locking, queued status and failure recovery."""
import importlib.util
import json
from pathlib import Path
import threading
import time
import tkinter as tk
from tkinter import ttk
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("manager", Path(__file__).parents[1] / "tools/rotorrec_manager.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class ManagerGuiTests(unittest.TestCase):
    def test_status_connection_lock_and_failure_recovery(self):
        root = tk.Tk()
        root.withdraw()
        release = threading.Event()
        entered = threading.Event()
        calls = []

        def device(args, log, passthrough, cancelled):
            calls.append(args)
            entered.set()
            if not release.wait(3):
                raise RuntimeError("test worker timeout")
            if len(calls) == 2:
                raise RuntimeError("injected transport failure")
            log(json.dumps({"settings_raw": m.encode_settings(dict(user_mode_id=42, osd_lines=4,
                led_percent=25, low_battery_percent=20, link_paused=False, owns_extra_lines=True))}))

        def widgets(parent):
            for child in parent.winfo_children():
                yield child
                yield from widgets(child)

        def pump_until(predicate):
            deadline = time.monotonic() + 3
            while not predicate() and time.monotonic() < deadline:
                root.update()
                time.sleep(.01)
            self.assertTrue(predicate(), "GUI completion timeout")

        def exercise():
            all_widgets = list(widgets(root))
            combos = [w for w in all_widgets if isinstance(w, ttk.Combobox)]
            port, camera, user, osd, led, battery = combos
            uart = next(w for w in all_widgets if isinstance(w, ttk.Entry) and not isinstance(w, ttk.Combobox))
            direct = next(w for w in all_widgets if isinstance(w, ttk.Checkbutton))
            buttons = {w.cget("text"): w for w in all_widgets if isinstance(w, ttk.Button)}
            output = next(w for w in all_widgets if isinstance(w, tk.Text))
            port.set("FAKE_A")
            buttons["Save settings & restart"].invoke()
            self.assertEqual(calls, [])
            self.assertIn("Read status on this connection", output.get("1.0", "end"))
            buttons["Read status"].invoke()
            self.assertTrue(entered.wait(1))
            for control in (*combos, uart, direct):
                self.assertEqual(str(control.cget("state")), "disabled")
            buttons["Read status"].invoke()
            self.assertEqual(len(calls), 1)
            release.set()
            pump_until(lambda: str(port.cget("state")) == "normal")
            self.assertEqual([w.get() for w in (user, osd, led, battery)], ["3", "4", "25", "20"])
            for control in (camera, user, osd):
                self.assertEqual(str(control.cget("state")), "readonly")
            for control in (port, uart, led, battery, direct):
                self.assertEqual(str(control.cget("state")), "normal")
            # A new connection must read its own status before saving settings.
            port.set("FAKE_B")
            port.event_generate("<<ComboboxSelected>>")
            buttons["Save settings & restart"].invoke()
            self.assertEqual(len(calls), 1)
            entered.clear()
            release.clear()
            buttons["Read status"].invoke()
            self.assertTrue(entered.wait(1))
            release.set()
            pump_until(lambda: str(port.cget("state")) == "normal")
            self.assertIn("injected transport failure", output.get("1.0", "end"))
            self.assertEqual(calls[1].port, "FAKE_B")
            self.assertEqual(str(camera.cget("state")), "readonly")
            buttons["Save settings & restart"].invoke()
            self.assertEqual(len(calls), 2)

        try:
            with patch("tkinter.Tk", return_value=root), patch.object(root, "mainloop", exercise), \
                 patch("serial.tools.list_ports.comports", return_value=[]), patch.object(m, "run_device", device):
                m.gui()
        finally:
            release.set()
            root.destroy()


if __name__ == "__main__":
    unittest.main()
