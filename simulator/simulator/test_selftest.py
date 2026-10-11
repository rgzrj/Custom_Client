"""发布离线自检不得启动网络、不得覆盖已有报告。"""

import json
import tempfile
import tkinter as tk
import unittest
from pathlib import Path
from unittest.mock import patch

from simulator.control_console import main


class OfflineSelfTestTests(unittest.TestCase):
    def setUp(self):
        try:
            root = tk.Tk()
            root.withdraw()
            root.destroy()
        except tk.TclError as error:
            self.skipTest(str(error))

    def test_hidden_report_without_network(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "report.json"
            with patch("sys.argv", ["control_console.py", "--self-test-report", str(report)]), \
                    patch("simulator.control_console.ConsoleSession.start") as start:
                main()
                start.assert_not_called()
            result = json.loads(report.read_text(encoding="utf-8"))
            self.assertTrue(result["ok"])
            self.assertFalse(result["networkStarted"])
            self.assertFalse(result["cameraOpened"])
            self.assertFalse(result["frozen"])

    def test_existing_report_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "report.json"
            report.touch()
            with patch("sys.argv", ["control_console.py", "--self-test-report", str(report)]), \
                    patch("simulator.control_console.ConsoleSession.start") as start:
                with self.assertRaises(FileExistsError):
                    main()
                start.assert_not_called()
            self.assertEqual(report.read_bytes(), b"")


if __name__ == "__main__":
    unittest.main()
