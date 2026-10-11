"""Tk editor/scene smoke tests without visible windows or external services."""

import tempfile
import tkinter as tk
import unittest
from pathlib import Path
from unittest.mock import patch

from simulator.control_console import ControlConsole
from simulator.sim_state import read_json


class ControlConsoleTests(unittest.TestCase):
    def setUp(self):
        try:
            self.root = tk.Tk()
        except tk.TclError as error:
            self.skipTest(str(error))
        self.root.attributes("-alpha", 0)
        self.app = ControlConsole(self.root)
        self.root.update()

    def tearDown(self):
        if hasattr(self, "app"):
            self.app.session.stop()
            if self.app.session.worker:
                self.app.session.worker.join(4)
            self.root.after_cancel(self.app.poll_id)
            self.root.destroy()

    def test_editor_and_robot_identity_round_trip(self):
        scene = self.app.collect()
        self.app.vars["team"].set("red")
        self.app.vars["self_index"].set("步兵4")
        self.app.robot_vars["red", 3, "hp"].set("0")
        changed = self.app.collect()
        self.assertEqual(changed["state"]["team"], "red")
        self.assertEqual(changed["state"]["self_index"], 3)
        self.assertEqual(changed["state"]["robots"]["red"][3]["hp"], 0)
        self.app.set_scene(scene)
        self.assertEqual(self.app.collect(), scene)

    def test_scene_save_load_and_preset(self):
        self.app.preset.set("独立断流恢复")
        self.app.fill_preset()
        expected = self.app.collect()
        self.assertEqual(len(expected["steps"]), 4)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "scene.json"
            with patch("simulator.control_console.filedialog.asksaveasfilename", return_value=str(path)):
                self.app.save_scene()
            self.assertEqual(read_json(path), expected)
            self.app.vars["team"].set("red")
            with patch("simulator.control_console.filedialog.askopenfilename", return_value=str(path)):
                self.app.load_scene()
            self.assertEqual(self.app.collect(), expected)
        self.assertFalse(self.app.session.running)

    def test_structure_editor_and_legacy_unknown(self):
        scene = self.app.collect()
        self.app.structure_vars["red", "base", "hp"].set("0")
        self.assertEqual(self.app.collect()["state"]["structures"]["red"]["base"]["hp"], 0)
        scene["state"].pop("structures")
        self.app.set_scene(scene)
        self.assertTrue(all(var.get() == "" for var in self.app.structure_vars.values()))
        self.assertNotIn("structures", self.app.collect()["state"])

    def test_small_window_keeps_tabs_scrollable(self):
        self.root.geometry("1020x740")
        self.root.update()
        for canvas in self.app.scrollers:
            self.assertEqual(len(canvas.find_all()), 1)
            self.assertTrue(canvas.itemcget(canvas.find_all()[0], "window"))
            self.assertTrue(canvas.cget("yscrollcommand"))
        self.assertLessEqual(self.root.winfo_reqheight(), 740)
        self.assertFalse(self.app.session.running)


if __name__ == "__main__":
    unittest.main()
