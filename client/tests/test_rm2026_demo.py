"""Exercise the exact user-facing synthetic scenario with the native viewer."""

import argparse
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

from mqtt_wire import read_packet
import rm2026_observer_demo as demo


class DemoChecks(unittest.TestCase):
    def test_unavailable_log_does_not_prevent_demo(self):
        with tempfile.TemporaryDirectory() as directory:
            blocker = Path(directory) / "blocked"
            blocker.write_text("not a directory", encoding="utf-8")
            with patch.dict(os.environ, {"RM27_ARTIFACT_ROOT": str(blocker)}), \
                    patch.object(sys, "argv", [str(DEMO), "--exe", str(EXE)]), \
                    patch.object(demo, "run") as run, patch.object(sys, "stderr", io.StringIO()):
                self.assertEqual(demo.main(), 0)
                run.assert_called_once()

    def test_hud_runtime_log_from_other_working_directory(self):
        if EXE.name != "rm27_terminal.exe":
            self.skipTest("Historical QML HUD build only")
        with tempfile.TemporaryDirectory() as directory, socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]  # No listening broker; cannot connect externally.
            root = Path(directory)
            result = subprocess.run([str(EXE), "--protocol", "rm2026-v2.0.0",
                "--synthetic-demo", "--port", str(port), "--duration-ms", "300"], cwd=root,
                env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "RM27_ARTIFACT_ROOT": str(root)},
                capture_output=True, text=True, encoding="utf-8", timeout=4)
            self.assertEqual(result.returncode, 0, result.stderr)
            reports = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertTrue(reports)
            self.assertTrue(all(not value["hud"]["armed"] for value in reports))
            logs = list((root / "log").glob("rm27-run-*.log"))
            self.assertEqual(len(logs), 1)
            content = logs[0].read_text(encoding="utf-8")
            self.assertTrue(content.startswith("RM27-RUNTIME-LOG v1\nSTART"))
            self.assertIn("STATE mode=", content)
            self.assertIn("armed=0", content)
            self.assertIn("END dropped=", content)
            self.assertFalse(list((root / "log").glob("*.lock")))
            self.assertTrue((root / "pitcure/temp").is_dir())

    def test_hud_requires_explicit_local_mode(self):
        if EXE.name != "rm27_terminal.exe":
            self.skipTest("Historical QML HUD build only")
        valid = ["--protocol", "rm2026-v2.0.0", "--synthetic-demo", "--duration-ms", "100"]
        for args in ([], ["--protocol", "rm2026-v2.0.0"],
                     [*valid, "--host", "192.168.1.1"], [*valid, "--host", "localhost"],
                     [*valid, "--port", "0"], [*valid, "--port", "65536"],
                     ["--protocol", "rm2027", "--synthetic-demo"],
                     [*valid, "--duration-ms", "0"],
                     [*valid, "--sim-video-port", "0"], [*valid, "--sim-video-port", "65536"],
                     [*valid, "--sim-video-port", "abc"],
                     [*valid, "--robot-id", "03"], [*valid, "--robot-id", "5"],
                     [*valid, "--robot-id", "107x"], [*valid, "--fragment-base", "0"],
                     [*valid, "--hevc-video-port", "12345"],
                     [*valid, "--hevc-video-port", "0", "--fragment-base", "0"],
                     [*valid, "--hevc-video-port", "12345", "--fragment-base", "2"],
                     [*valid, "--hevc-video-port", "12345", "--fragment-base", "0", "--sim-video-port", "12346"]):
            result = subprocess.run([str(EXE), *args], env={**os.environ, "QT_QPA_PLATFORM": "offscreen"},
                capture_output=True, text=True, timeout=4)
            self.assertEqual(result.returncode, 2, args)
            self.assertEqual(result.stdout, "", args)

    def test_startup_script_encoding(self):
        root = Path(__file__).resolve().parents[1]
        for name in ("run-rm2026-demo.ps1", "run-rm2026-observer.ps1", "environment.ps1"):
            data = (root / "scripts" / name).read_bytes()
            # Windows PowerShell 5.1 treats BOM-less scripts as the ANSI codepage.
            self.assertTrue(data.isascii() or data.startswith(b"\xef\xbb\xbf"), name)

    @unittest.skipUnless(sys.platform == "win32", "Windows PowerShell compatibility")
    def test_windows_powershell_startup(self):
        root = Path(__file__).resolve().parents[1]
        shell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        result = subprocess.run([str(shell), "-NoProfile", "-File",
            str(root / "scripts/run-rm2026-demo.ps1"), "-Python", sys.executable,
            "-Executable", str(EXE)],
            env={**os.environ, "QT_QPA_PLATFORM": "offscreen"}, cwd=root,
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=50)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        phases = [json.loads(line)["phase"] for line in result.stdout.splitlines() if line.startswith("{")]
        self.assertEqual(phases, ["normal", "expired", "offline", "recovered", "complete"])

    def test_remaining_length_rejected_before_body_read(self):
        data = io.BytesIO(b"\x30\x81\x20")  # 4097 bytes, no body supplied.
        stream = Mock(recv=Mock(side_effect=data.read))
        with self.assertRaisesRegex(ValueError, "exceeds limit"):
            read_packet(stream)
        self.assertEqual(stream.recv.call_count, 3)

    def test_synthetic_scenario(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, "-S", str(DEMO), "--exe", str(EXE),
                "--phase-seconds", "2"], env={**os.environ, "RM27_ARTIFACT_ROOT": directory},
                capture_output=True, text=True, encoding="utf-8", timeout=40)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            logs = list((Path(directory) / "log").glob("rm27-demo-*.log"))
            self.assertEqual(len(logs), 1)
            content = logs[0].read_text(encoding="utf-8")
            self.assertTrue(content.startswith("RM27-RUNTIME-LOG v1\nSTART synthetic"))
            self.assertIn("PHASE normal", content)
            self.assertIn("PHASE expired", content)
            self.assertIn("PHASE recovered", content)
            self.assertTrue(content.endswith("END\n"))
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        reports = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([value["phase"] for value in reports],
                         ["normal", "expired", "offline", "recovered", "complete"])
        self.assertTrue(all(value["synthetic"] for value in reports))
        self.assertEqual(len({value["port"] for value in reports}), 1)
        times = [value["elapsed_ms"] for value in reports]
        self.assertEqual(times, sorted(times))
        self.assertGreaterEqual(times[1] - times[0], 1500)
        normal, expired, offline, restored, _ = reports
        self.assertEqual(normal["link"], "subscribed")
        self.assertEqual(len(normal["state"]), 13)
        self.assertEqual(normal["state"]["latest_buff"], {"reported_robot_id": 101, "type_code": 2,
                         "kind": "defense_or_vulnerability", "level": -1, "max_time": 10, "left_time": 0})
        self.assertEqual(normal["state"]["performance"], {"shooter": 0, "chassis": 2, "sentry_control": 0})
        self.assertEqual(normal["state"]["deployment"], {"code": 0, "state": "not_deployed"})
        self.assertEqual(normal["state"]["self_config"], {"robot_id": 101, "team": "blue",
                                                         "max_health": 1000, "level": 3})
        self.assertEqual(normal["state"]["self_dynamic"]["health"], 850)
        self.assertEqual(normal["state"]["self_dynamic"]["permitted_shots"], 80)
        self.assertIs(normal["state"]["game"]["paused"], False)
        self.assertEqual(normal["state"]["game"]["red_score"], 0)
        self.assertEqual(normal["state"]["game"]["blue_score"], 2)
        self.assertEqual(normal["state"]["game"]["elapsed_sec"], 0)
        self.assertIs(normal["state"]["respawn"]["pending"], False)
        self.assertEqual(normal["state"]["latest_event"],
                         {"id": 0, "kind": "unknown", "param": "synthetic-local-demo"})
        self.assertEqual(normal["state"]["position"],
                         {"x": 2.5, "y": -1, "yaw_deg": 30, "reported_robot_id": 107})
        self.assertEqual(expired["link"], "subscribed")
        self.assertEqual(expired["state"], {})
        self.assertEqual(len(expired["stale"]), 13)
        self.assertEqual(offline["link"], "offline")
        self.assertEqual(offline["state"], {})
        self.assertEqual(offline["stale"], [])
        self.assertEqual(restored["link"], "subscribed")
        self.assertEqual(restored["state"]["self_config"]["team"], "red")
        self.assertEqual(restored["state"]["self_dynamic"], {"health": 500})
        self.assertEqual(restored["state"]["game"], {"stage_code": 4, "phase": "playing"})
        self.assertEqual(restored["state"]["latest_buff"], {"level": 0, "kind": "unknown"})
        self.assertEqual(restored["state"]["performance"], {"sentry_control": 99})
        self.assertEqual(restored["state"]["deployment"], {"code": 99, "state": "unknown"})
        if EXE.name == "rm27_terminal.exe":
            self.assertIn("局 1/3 · 红 0:2 蓝 · 剩余 420 s · 已过 0 s · 未暂停", normal["hud"]["alert"])
            for value in (expired, offline):
                self.assertIn("比赛数据未知 / 已过期", value["hud"]["alert"])
                self.assertNotIn("红 0:2 蓝", value["hud"]["alert"])
            self.assertIn("局 --/-- · 红 --:-- 蓝 · 剩余 -- s · 已过 -- s · 暂停未知", restored["hud"]["alert"])
            self.assertEqual(normal["hud"]["hp"], "850 / 1000")
            self.assertEqual(normal["hud"]["data"]["friendlyCoins"], 100)
            self.assertEqual(normal["hud"]["data"]["friendlyHp"], [1000, None, 1000, 1000, 1000, 1000])
            self.assertEqual(normal["hud"]["data"]["structures"]["friendly_base"], {"hp": 5000})
            self.assertEqual(normal["hud"]["data"]["structures"]["enemy_outpost"], {"life": "destroyed"})
            self.assertTrue(normal["hud"]["messages"])
            self.assertLessEqual(len(normal["hud"]["messages"]), 24)
            self.assertTrue(all("历史测试" in item["text"] for item in normal["hud"]["messages"]))
            self.assertTrue(expired["hud"]["messages"])  # Clearly labelled history is not fresh state.
            self.assertEqual(offline["hud"]["messages"], [])
            self.assertTrue(restored["hud"]["messages"])
            old_ids = {item["id"] for item in expired["hud"]["messages"]}
            self.assertTrue(all(item["id"] not in old_ids for item in restored["hud"]["messages"]))
            for value in (expired, offline):
                self.assertEqual(value["hud"]["data"], {})
                self.assertEqual(value["hud"]["hp"], "-- / --")
                self.assertIs(value["hud"]["fresh"], False)
            self.assertEqual(restored["hud"]["hp"], "500 / 1000")
            self.assertEqual(restored["hud"]["data"]["ownTeam"], "red")
            self.assertNotIn("allowedShots", restored["hud"]["data"])
            for value in (normal, expired, offline, restored):
                self.assertFalse(value["hud"]["armed"])
                self.assertEqual(value["hud"]["map"], {})
                self.assertIn("RM2026", value["hud"]["source"])

    def test_invalid_arguments(self):
        for args in (("--exe", "does-not-exist.exe"),
                     ("--exe", str(EXE), "--phase-seconds", "0"),
                     ("--exe", str(EXE), "--video-ts", "does-not-exist.ts"),
                     ("--exe", str(EXE), "--video-ts", str(DEMO)),
                     ("--exe", str(EXE), "--hevc-file", str(DEMO)),
                     ("--exe", str(EXE), "--hevc-file", "missing.hevc"),
                     ("--exe", str(EXE), "--video-faults"),
                     ("--exe", str(EXE), "--robot-id", "03"),
                     ("--exe", str(EXE), "--robot-id", "5")):
            result = subprocess.run([sys.executable, "-S", str(DEMO), *args],
                capture_output=True, text=True, encoding="utf-8", timeout=3)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")

    def test_video_scenario(self):
        if not VIDEO_TS or EXE.name != "rm27_terminal.exe":
            self.skipTest("Explicit local TS fixture and historical HUD build required")
        result = subprocess.run([sys.executable, "-S", str(DEMO), "--exe", str(EXE),
            "--phase-seconds", "2", "--video-ts", str(VIDEO_TS)],
            env={**os.environ, "QT_QPA_PLATFORM": "offscreen"},
            capture_output=True, text=True, encoding="utf-8", timeout=50)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        reports = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([value["phase"] for value in reports], ["normal", "video_expired",
            "video_recovered", "expired", "offline", "recovered", "complete"])
        normal, stopped, resumed, expired, offline, restored, _ = reports
        self.assertGreater(normal["video"]["decodedFrames"], 5)
        self.assertIs(stopped["video"]["stale"], True)
        self.assertIs(stopped["video"]["fresh"], False)
        self.assertIn("已过期", stopped["video"]["label"])
        self.assertGreaterEqual(stopped["video"]["frameAgeMs"], 700)
        self.assertGreater(resumed["video"]["decodedFrames"], stopped["video"]["decodedFrames"] + 5)
        self.assertGreaterEqual(resumed["video"]["recoveryAfterStaleMs"], 0)
        for value in (normal, stopped, resumed, restored):
            self.assertIs(value["hud"]["fresh"], True)
        for value in (normal, resumed, expired):
            self.assertIs(value["video"]["fresh"], True, (value["phase"], value["video"]))
            self.assertIs(value["video"]["stale"], False)
            self.assertIn("测试 TS 已出帧", value["video"]["stage"])
        for value in (expired, offline):
            self.assertIs(value["hud"]["fresh"], False)
            self.assertEqual(value["hud"]["data"], {})
        # Video sender explicitly stops after the independent expiry check; MQTT still recovers.
        self.assertIs(restored["video"]["stale"], True)
        self.assertIs(restored["video"]["fresh"], False)
        for value in reports[:-1]:
            self.assertIs(value["hud"]["armed"], False)
            self.assertIs(value["video"]["endToEndMeasured"], False)

    def test_hevc_scenario(self):
        if not HEVC_FILE or EXE.name != "rm27_terminal.exe":
            self.skipTest("Generated HEVC fixture and historical HUD build required")
        result = subprocess.run([sys.executable, "-S", str(DEMO), "--exe", str(EXE),
            "--phase-seconds", "2", "--hevc-file", str(HEVC_FILE), "--robot-id", "3",
            "--fragment-base", "1", "--video-faults"], env={**os.environ, "QT_QPA_PLATFORM": "offscreen"},
            capture_output=True, text=True, encoding="utf-8", timeout=50)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        reports = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([value["phase"] for value in reports], ["normal", "video_expired",
            "video_recovered", "video_damaged", "video_keyframe_recovered",
            "expired", "offline", "recovered", "complete"])
        normal, stopped, resumed, damaged, keyframe, expired, offline, restored, _ = reports
        # Requested CONNECT identity is not authoritative robot status or hardware pairing.
        self.assertEqual(normal["state"]["self_config"]["robot_id"], 101)
        self.assertEqual(restored["state"]["self_config"]["robot_id"], 1)
        self.assertGreater(normal["video"]["decodedFrames"], 5)
        self.assertTrue(stopped["video"]["stale"])
        self.assertFalse(stopped["video"]["fresh"])
        self.assertGreaterEqual(stopped["video"]["frameAgeMs"], 700)
        self.assertGreater(resumed["video"]["decodedFrames"], stopped["video"]["decodedFrames"] + 5)
        self.assertGreater(normal["video"]["transport"]["decodedFrames"], 5)
        self.assertGreater(normal["video"]["transport"]["duplicates"], 0)
        self.assertTrue(damaged["video"]["stale"])
        self.assertFalse(damaged["video"]["fresh"])
        self.assertGreaterEqual(damaged["video"]["frameAgeMs"], 700)
        self.assertTrue(damaged["video"]["transport"]["waitingForKeyframe"])
        for counter in ("packets", "assembledFrames", "rejectedPackets", "invalidHevcUnits", "droppedFrames"):
            self.assertGreater(damaged["video"]["transport"][counter], resumed["video"]["transport"][counter])
        self.assertFalse(keyframe["video"]["transport"]["waitingForKeyframe"])
        self.assertGreater(keyframe["video"]["decodedFrames"], damaged["video"]["decodedFrames"] + 5)
        for value in reports[:-1]:
            self.assertEqual(value["client_id"], "3")
            self.assertFalse(value["hud"]["armed"])
            self.assertTrue(value["video"]["synthetic"])
            self.assertIn("本机合成测试", value["hud"]["source"])
            self.assertFalse(value["video"]["endToEndMeasured"])
        for value in (normal, resumed, keyframe, expired, offline):
            self.assertTrue(value["video"]["fresh"], value)
            self.assertIn("HEVC 已出帧", value["video"]["stage"])
        # A new reported identity must stop the old native HEVC session.
        self.assertFalse(restored["video"]["fresh"])
        self.assertEqual(restored["video"]["transport"], {})
        for value in (normal, stopped, resumed, damaged, keyframe, restored):
            self.assertTrue(value["hud"]["fresh"])
        for value in (expired, offline):
            self.assertFalse(value["hud"]["fresh"])
            self.assertEqual(value["hud"]["data"], {})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--video-ts", type=Path)
    parser.add_argument("--hevc-file", type=Path)
    options, remaining = parser.parse_known_args()
    EXE = options.exe.resolve()
    VIDEO_TS = options.video_ts
    HEVC_FILE = options.hevc_file
    DEMO = Path(__file__).with_name("rm2026_observer_demo.py")
    unittest.main(argv=[sys.argv[0], *remaining])
