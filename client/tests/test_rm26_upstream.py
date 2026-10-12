"""Exercise the imported backend and existing HUD in the ordinary executable."""
import argparse
import json
import os
from pathlib import Path
import queue
import re
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest

from mqtt_wire import packet, publish, read_packet

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = os.environ.get("QT_QPA_PLATFORM", "offscreen")
TOPICS = ["GameStatus", "GlobalUnitStatus", "GlobalLogisticsStatus", "GlobalSpecialMechanism",
    "Event", "RobotInjuryStat", "RobotRespawnStatus", "RobotStaticStatus", "RobotDynamicStatus",
    "RobotModuleStatus", "RobotPosition", "Buff", "PenaltyInfo", "RobotPathPlanInfo", "MapClickInfo",
    "CustomByteBlock", "TechCoreMotionStateSync", "RobotPerformanceSelectionSync", "DeployModeStatusSync",
    "RuneStatusSync", "SentryStatusSync", "DartSelectTargetStatusSync", "SentryCtrlResult", "AirSupportStatusSync"]


def varint(value):
    value &= (1 << 64) - 1
    result = bytearray()
    while value > 127:
        result.append((value & 127) | 128)
        value >>= 7
    return bytes(result + bytes([value]))


SCHEMA = {name: re.findall(r"(optional|repeated)\s+(\w+)\s+(\w+)\s*=\s*(\d+)", body)
    for name, body in re.findall(r"message\s+(\w+)\s*\{([^}]+)\}",
        (ROOT / "contracts/rm2026-v2.0.0/client.proto").read_text(encoding="utf-8"))}


def message(name, overrides=None, only=False):
    overrides = overrides or {}
    body = b""
    for label, kind, field, number in SCHEMA[name]:
        if only and field not in overrides:
            continue
        if label == "repeated" and field not in overrides:
            continue
        value = overrides.get(field, b"" if kind == "bytes" else "" if kind == "string" else 0)
        values = value if label == "repeated" else [value]
        for value in values:
            if kind == "float":
                body += varint(int(number) * 8 + 5) + struct.pack("<f", value)
            elif kind in ("bytes", "string"):
                data = value.encode() if kind == "string" else value
                body += varint(int(number) * 8 + 2) + varint(len(data)) + data
            else:
                body += varint(int(number) * 8) + varint(value)
    return body


class Rig:
    def __init__(self, duration=12000, extra_args=()):
        self.temp = tempfile.TemporaryDirectory()
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(2)
        self.listener.settimeout(9)
        self.reports = queue.Queue()
        self.stderr = []
        self.process = subprocess.Popen([str(EXE), "--protocol", "rm2026-v2.0.0",
            "--mqtt-host", "127.0.0.1", "--mqtt-port", str(self.listener.getsockname()[1]),
            "--robot-id", "3", "--protocol-test", "--diagnostics-json", "--duration-ms", str(duration), *extra_args],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
            env={**os.environ, "QT_QPA_PLATFORM": PLATFORM, "RM27_ARTIFACT_ROOT": self.temp.name,
                "RM27_SETTINGS_FILE": str(Path(self.temp.name) / "settings.ini")})
        self.reader = threading.Thread(target=self.read_reports, daemon=True)
        self.reader.start()
        self.error_reader = threading.Thread(target=lambda: self.stderr.extend(self.process.stderr), daemon=True)
        self.error_reader.start()
        self.stream = None

    def read_reports(self):
        for line in self.process.stdout:
            self.reports.put(json.loads(line))

    def accept(self):
        self.stream, _ = self.listener.accept()
        self.stream.settimeout(4)
        header, body = read_packet(self.stream)
        assert header == 0x10 and body[:10] == b"\x00\x04MQTT\x04\x02\x00\x14", body
        assert body[10:] == b"\x00\x013", body
        self.stream.sendall(packet(0x20, b"\0\0"))

    def subscriptions(self, first_delay=0, first_grant=1):
        topics = []
        for index in range(24):
            header, body = read_packet(self.stream)
            assert header == 0x82, header
            length = int.from_bytes(body[2:4], "big")
            topics.append(body[4:4+length].decode())
            assert body[4+length:] == b"\x01", body
            if index == 0 and first_delay:
                # UI diagnostics continue while the backend waits for SUBACK.
                # Native Windows QML startup can exceed the deliberate SUBACK delay.
                # Allow initial GUI readiness, then still withhold ACK for first_delay.
                report = self.wait(lambda r: r.get("hud") is not None)
                assert not report["hud"]["fresh"], report
                time.sleep(first_delay)
            grant = first_grant if index == 0 else 1
            self.stream.sendall(packet(0x90, body[:2] + bytes([grant])))
            if grant != 1:
                return
        assert topics == TOPICS, topics

    def wait(self, predicate, timeout=4):
        until = time.monotonic() + timeout
        last = None
        while time.monotonic() < until:
            try:
                last = self.reports.get(timeout=max(0.01, until - time.monotonic()))
            except queue.Empty:
                break
            if predicate(last):
                return last
        raise AssertionError(f"No expected report; last={last}; stderr={''.join(self.stderr)[-2000:]}")

    def close(self):
        if self.stream:
            self.stream.close()
        self.listener.close()
        try:
            self.process.wait(timeout=8) # Supported 5s SUBACK timeout plus Paho thread teardown.
        except subprocess.TimeoutExpired:
            self.process.terminate()
            self.process.wait(timeout=4)
        self.reader.join(2)
        self.error_reader.join(2)
        self.process.stdout.close()
        self.process.stderr.close()
        self.temp.cleanup()
        assert self.process.returncode == 0, f"Client did not shut down cleanly: {self.process.returncode}; {''.join(self.stderr)[-3000:]}"


class BackendChecks(unittest.TestCase):
    def test_video_and_mqtt_fail_independently(self):
        fixture = os.environ.get("RM27_TEST_HEVC")
        if not fixture:
            self.skipTest("No generated HEVC fixture")
        data = Path(fixture).read_bytes()
        starts = []
        for match in re.finditer(b"\x00\x00(?:\x00)?\x01", data):
            if (data[match.end()] >> 1) == 35:
                starts.append(match.start())
        self.assertGreater(len(starts), 30)
        starts.append(len(data))
        frames = [data[a:b] for a, b in zip(starts, starts[1:])]
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addCleanup(sender.close)
        sender.bind(("127.0.0.1", 0))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        rig = Rig(6500, ["--hevc-udp", "--video-bind", "127.0.0.1", "--video-source", "127.0.0.1",
            "--video-port", str(port), "--fragment-base", "0"])
        self.addCleanup(rig.close)
        rig.accept(); rig.subscriptions()
        rig.wait(lambda r: r.get("link") == "subscribed")
        publish(rig.stream, "RobotStaticStatus", message("RobotStaticStatus", {"robot_id": 3, "max_health": 400}, True))
        publish(rig.stream, "RobotDynamicStatus", message("RobotDynamicStatus", {"current_health": 77}, True))
        rig.wait(lambda r: r["hud"]["hp"] == "77 / 400")
        def send_frame(sequence, frame):
            for slice_id in reversed(range((len(frame) + 1391)//1392)):
                sender.sendto(struct.pack("!HHI", sequence, slice_id, len(frame))
                    + frame[slice_id*1392:(slice_id+1)*1392], ("127.0.0.1", port))
        for index in range(35):
            send_frame(index, frames[index % len(frames)]); time.sleep(0.033)
        rig.wait(lambda r: r.get("video", {}).get("fresh") and r["video"]["decodedFrames"] > 10)
        time.sleep(0.9)
        publish(rig.stream, "RobotStaticStatus", message("RobotStaticStatus", {"robot_id": 3, "max_health": 400}, True))
        publish(rig.stream, "RobotDynamicStatus", message("RobotDynamicStatus", {"current_health": 88}, True))
        report = rig.wait(lambda r: r["hud"]["hp"] == "88 / 400" and r["video"]["stale"])
        self.assertFalse(report["video"]["fresh"])
        rig.stream.shutdown(socket.SHUT_RDWR); rig.stream.close(); rig.stream = None
        rig.wait(lambda r: r.get("link") == "offline" and not r["hud"]["fresh"])
        for index in range(25):
            send_frame(index, frames[index % len(frames)]); time.sleep(0.033)
        report = rig.wait(lambda r: r["video"]["fresh"] and not r["hud"]["fresh"])
        self.assertFalse(report["hud"]["armed"])
        self.assertEqual(report["video"]["packetCountersSource"], "rm2026-hevc-udp")

    def test_reports_hud_expiry_identity_disconnect_reconnect(self):
        rig = Rig()
        self.addCleanup(rig.close)
        rig.accept()
        rig.subscriptions(first_delay=0.6)
        rig.wait(lambda r: r.get("link") == "subscribed")
        fixtures = {
            "RobotStaticStatus": {"robot_id": 3, "max_health": 400, "heat_cooldown_rate": 1.25,
                "robot_type": 255, "max_heat": 500, "max_power": 4294967295,
                "max_buffer_energy": 0, "max_chassis_energy": 42},
            "RobotDynamicStatus": {"current_health": 0, "current_heat": 2.5, "remaining_ammo": 0,
                "current_chassis_energy": 4294967295, "current_buffer_energy": 0,
                "current_experience": 4294967295, "experience_for_upgrade": 0,
                "total_projectiles_fired": 4294967295, "out_of_combat_countdown": 0},
            "GlobalUnitStatus": {"robot_health": list(range(10)),
                "robot_bullets": [-2147483648,-1,0,2147483647,1,2,3,4,5,6],
                "total_damage_ally": 4294967295, "total_damage_enemy": 0},
            "GlobalLogisticsStatus": {"remaining_economy": 4294967295,
                "total_economy_obtained": 18446744073709551615, "tech_level": 255, "encryption_level": 0},
            "GlobalSpecialMechanism": {"mechanism_id": [1, 2, 255], "mechanism_time_sec": [-1, 0, -2147483648]},
            "RobotInjuryStat": {"total_damage": 4294967295, "collision_damage": 2,
                "small_projectile_damage": 3, "large_projectile_damage": 4, "dart_splash_damage": 5,
                "module_offline_damage": 6, "offline_damage": 7, "penalty_damage": 8,
                "server_kill_damage": 9, "killer_id": 4294967295},
            "GameStatus": {"current_stage": 4, "stage_countdown_sec": -1, "is_paused": False},
            "Event": {"event_id": 1, "param": "local event"},
            "RobotPathPlanInfo": {"intention": 3, "start_pos_x": 1, "start_pos_y": 4294967295,
                "offset_x": [-128, 127], "offset_y": [127, -128], "sender_id": 7},
            "MapClickInfo": {"is_send_all": 2, "robot_id": bytes([101,102,3,255,0,0,0]),
                "mode": 4, "enemy_id": 255, "ascii": 90, "type": 1, "map_x": 1.25, "map_y": -2.5},
            "CustomByteBlock": {"data": b"\x00\xff"},
            "TechCoreMotionStateSync": {"maximum_difficulty_level": 4, "basic_state": 3,
                "putin_state": 1, "move_state": 1, "rotate_state": 1, "enemy_core_status": 2,
                "remain_time_all": 60, "remain_time_step": 0},
            "RuneStatusSync": {"rune_status": 2, "activated_arms": 3, "average_rings": 7.5},
            "SentryStatusSync": {"posture_id": 2, "is_weakened": False, "is_powered": True},
            "DartSelectTargetStatusSync": {"target_id": 5, "open": 1},
        }
        # Establish identity before unrelated state; a later identity change resets it.
        for topic in ["RobotStaticStatus", *[t for t in TOPICS if t != "RobotStaticStatus"]]:
            publish(rig.stream, topic, message(topic, fixtures.get(topic)), mid=TOPICS.index(topic)+1)
        report = rig.wait(lambda r: len(r.get("reports", {})) == 24 and r["hud"]["hp"] == "0 / 400")
        gui_stalls = report["gui_stalls"] # Exclude initial QML/font startup from I/O measurements.
        self.assertEqual(report["hud"]["hp"], "0 / 400")
        self.assertEqual(report["hud"]["buffer_energy"], "0 J")
        self.assertEqual(report["hud"]["heat"], "2.5")
        self.assertEqual(report["hud"]["power"], "--")
        self.assertEqual(report["hud"]["physical_ammo"], "--")
        self.assertFalse(report["hud"]["armed"])
        self.assertEqual(report["hud"]["map"], {})
        self.assertTrue(report["state"]["control_constraints"]["penalty_release_unverified"])
        self.assertTrue(report["test_source"])
        self.assertEqual(report["hud"]["data"]["selfIndex"], 2)
        self.assertEqual(report["hud"]["data"]["friendlyCoins"], 4294967295)
        fields = report["reports"]["GlobalLogisticsStatus"]["fields"]
        self.assertEqual(fields["total_economy_obtained"], "18446744073709551615")
        self.assertEqual(report["reports"]["RobotStaticStatus"]["fields"]["heat_cooldown_rate"], 1.25)
        self.assertEqual(report["reports"]["RobotPathPlanInfo"]["fields"]["offset_x"], [-128,127])
        for key, value in {"robot_type": 255, "max_heat": 500, "heat_cooldown_rate": 1.25,
                "max_power": 4294967295, "max_buffer_energy": 0, "max_chassis_energy": 42}.items():
            self.assertEqual(report["state"]["self_config"][key], value)
        for key, value in {"chassis_energy": 4294967295, "buffer_energy": 0, "experience": 4294967295,
                "experience_for_upgrade": 0, "total_projectiles_fired": 4294967295, "out_of_combat_countdown": 0}.items():
            self.assertEqual(report["state"]["self_dynamic"][key], value)
        self.assertEqual(report["state"]["units"]["robot_bullets"], fixtures["GlobalUnitStatus"]["robot_bullets"])
        self.assertEqual(report["state"]["units"]["total_damage_ally"], 4294967295)
        self.assertEqual(report["state"]["units"]["total_damage_enemy"], 0)
        self.assertEqual(report["state"]["economy"], {"remaining": 4294967295,
            "total_obtained": "18446744073709551615", "tech_level": 255, "encryption_level": 0})
        self.assertEqual(report["state"]["tech_core"], {"maximum_difficulty": 4, "basic_state": 3,
            "put_in": 1, "move": 1, "rotate": 1, "enemy_core_status": 2, "remain_time_all": 60, "remain_time_step": 0})
        self.assertEqual(report["state"]["rune"], {"code": 2, "activated_arms": 3, "average_rings": 7.5})
        self.assertEqual(report["state"]["sentry_status"],
            {"posture_id": 2, "posture": "defense", "weakened": False, "powered": True})
        self.assertEqual(report["state"]["dart_status"],
            {"target_id": 5, "target": "base_end_moving", "gate_code": 1, "gate": "opening"})
        self.assertEqual(report["state"]["special_mechanisms"], {"entries": [
            {"id": 1, "time_sec": -1, "kind": "ally_fortress_occupied"},
            {"id": 2, "time_sec": 0, "kind": "enemy_fortress_occupied"},
            {"id": 255, "time_sec": -2147483648, "kind": "unknown"}]})
        self.assertEqual(report["state"]["injury"], fixtures["RobotInjuryStat"])
        self.assertEqual(report["state"]["path_plan"], {"intention": 3, "start_x_dm": 1,
            "start_y_dm": 4294967295, "sender_id": 7, "offsets": [{"x": -128, "y": 127}, {"x": 127, "y": -128}]})
        self.assertEqual(report["state"]["map_click_report"], {"send_all": 2,
            "robot_ids": [101,102,3,255,0,0,0], "marking_type": 4, "enemy_id": 255,
            "ascii": 90, "marking_mode": 1, "x": 1.25, "y": -2.5})
        self.assertEqual(report["state"]["custom_bytes"], {"data_hex": "00ff"})
        publish(rig.stream, "CustomByteBlock", message("CustomByteBlock", {"data": b""}, True), mid=101)
        publish(rig.stream, "RobotPathPlanInfo", message("RobotPathPlanInfo", {"sender_id": 0}, True))
        publish(rig.stream, "MapClickInfo", message("MapClickInfo", {"mode": 255, "type": 4294967295, "map_x": 0}, True))
        # QoS1 zero-byte payload is a received empty active-list report, not a missing topic.
        publish(rig.stream, "GlobalSpecialMechanism", b"", mid=100)
        publish(rig.stream, "RobotInjuryStat", message("RobotInjuryStat", {"killer_id": 0}, True))
        publish(rig.stream, "SentryStatusSync", message("SentryStatusSync", {"posture_id": 255, "is_powered": False}, True))
        publish(rig.stream, "DartSelectTargetStatusSync", message("DartSelectTargetStatusSync", {"open": 4294967295}, True))
        publish(rig.stream, "RuneStatusSync", message("RuneStatusSync", {"rune_status": 255}, True))
        publish(rig.stream, "TechCoreMotionStateSync", message("TechCoreMotionStateSync",
            {"enemy_core_status": 255, "remain_time_step": 4294967295}, True))
        report = rig.wait(lambda r: r.get("state", {}).get("rune", {}).get("code") == 255
            and r["state"].get("dart_status", {}).get("gate_code") == 4294967295
            and r["state"].get("sentry_status", {}).get("posture_id") == 255
            and r["state"].get("tech_core", {}).get("remain_time_step") == 4294967295
            and r["state"].get("special_mechanisms") == {"entries": []}
            and r["state"].get("injury") == {"killer_id": 0}
            and r["state"].get("path_plan") == {"sender_id": 0, "offsets": []}
            and r["state"].get("map_click_report", {}).get("marking_mode") == 4294967295
            and r["state"].get("custom_bytes") == {"data_hex": ""})
        self.assertEqual(report["state"]["rune"], {"code": 255})
        self.assertEqual(report["state"]["sentry_status"], {"posture_id": 255, "posture": "unknown", "powered": False})
        self.assertEqual(report["state"]["dart_status"], {"gate_code": 4294967295, "gate": "unknown", "target": "unknown"})
        self.assertEqual(report["state"]["tech_core"], {"enemy_core_status": 255, "remain_time_step": 4294967295})
        self.assertEqual(report["reports"]["GlobalSpecialMechanism"]["fields"], {})
        self.assertEqual(report["state"]["injury"], {"killer_id": 0})
        self.assertEqual(report["state"]["path_plan"], {"sender_id": 0, "offsets": []})
        self.assertEqual(report["state"]["map_click_report"], {"marking_type": 255, "marking_mode": 4294967295, "x": 0})
        self.assertEqual(report["hud"]["map"], {})
        self.assertEqual(report["hud"]["data"]["selfIndex"], 2)
        self.assertEqual(report["reports"]["CustomByteBlock"]["fields"], {"data": ""})
        accepted = report["accepted"]
        publish(rig.stream, "RobotDynamicStatus", b"\x80")  # Malformed protobuf.
        publish(rig.stream, "RobotDynamicStatus", message("RobotDynamicStatus", {"current_heat": float("nan")}, True))
        publish(rig.stream, "RobotDynamicStatus", message("RobotDynamicStatus", {"current_health": 999}, True), retained=True)
        rig.wait(lambda r: r.get("rejected", 0) >= 2)
        publish(rig.stream, "RobotDynamicStatus", message("RobotDynamicStatus", {"current_heat": 0}, True))
        report = rig.wait(lambda r: r.get("accepted", 0) > accepted and r["hud"]["hp"] == "-- / --")
        self.assertNotIn("current_health", report["reports"]["RobotDynamicStatus"]["fields"])
        self.assertEqual(report["reports"]["RobotDynamicStatus"]["fields"]["current_heat"], 0)
        self.assertEqual(report["state"]["self_dynamic"], {"heat": 0})
        self.assertEqual(report["hud"]["buffer_energy"], "--")
        self.assertEqual(report["hud"]["heat"], "0")
        # Expiry is per topic. Keep dynamic fresh while static/global/game expires.
        until = time.monotonic() + 1.8
        while time.monotonic() < until:
            publish(rig.stream, "RobotDynamicStatus", message("RobotDynamicStatus", {"current_health": 6}, True))
            publish(rig.stream, "DartSelectTargetStatusSync", message("DartSelectTargetStatusSync", {"open": 0}, True))
            time.sleep(0.2)
        report = rig.wait(lambda r: "RobotStaticStatus" not in r.get("reports", {}) and r["hud"]["hp"] == "6 / --"
            and "rune" not in r.get("state", {}) and "sentry_status" not in r.get("state", {})
            and "tech_core" not in r.get("state", {})
            and "special_mechanisms" not in r.get("state", {}) and "injury" not in r.get("state", {})
            and "path_plan" not in r.get("state", {}) and "map_click_report" not in r.get("state", {})
            and "custom_bytes" not in r.get("state", {}))
        self.assertNotIn("friendlyCoins", report["hud"]["data"])
        self.assertEqual(report["hud"]["buffer_energy"], "--")
        self.assertEqual(report["hud"]["heat"], "--")
        self.assertNotIn("penalty", report["state"])
        self.assertNotIn("rune", report["state"])
        self.assertNotIn("sentry_status", report["state"])
        self.assertNotIn("tech_core", report["state"])
        self.assertEqual(report["state"]["dart_status"], {"gate_code": 0, "gate": "closed", "target": "unknown"})
        self.assertTrue(report["state"]["control_constraints"]["penalty_release_unverified"])
        publish(rig.stream, "RobotStaticStatus", message("RobotStaticStatus", {"robot_id": 103}, True))
        report = rig.wait(lambda r: r.get("hud", {}).get("data", {}).get("ownTeam") == "blue")
        self.assertEqual(report["hud"]["hp"], "-- / --")
        self.assertEqual(report["hud"]["messages"], [])
        self.assertEqual(report["hud"]["buffer_energy"], "--")
        self.assertEqual(report["hud"]["heat"], "--")
        self.assertNotIn("dart_status", report["state"])
        self.assertTrue(report["state"]["control_constraints"]["penalty_release_unverified"])
        publish(rig.stream, "TechCoreMotionStateSync", message("TechCoreMotionStateSync", {"remain_time_all": 0}, True))
        publish(rig.stream, "GlobalSpecialMechanism", b"")
        publish(rig.stream, "RobotInjuryStat", message("RobotInjuryStat", {"total_damage": 0}, True))
        publish(rig.stream, "RobotPathPlanInfo", message("RobotPathPlanInfo", {"sender_id": 0}, True))
        publish(rig.stream, "MapClickInfo", message("MapClickInfo", {"map_x": 0}, True))
        publish(rig.stream, "CustomByteBlock", message("CustomByteBlock", {"data": b""}, True))
        publish(rig.stream, "GlobalUnitStatus", message("GlobalUnitStatus", {"total_damage_enemy": 0}, True))
        publish(rig.stream, "GlobalLogisticsStatus", message("GlobalLogisticsStatus", {"encryption_level": 0}, True))
        report = rig.wait(lambda r: r.get("state", {}).get("tech_core") == {"remain_time_all": 0}
            and r["state"].get("special_mechanisms") == {"entries": []}
            and r["state"].get("injury") == {"total_damage": 0}
            and r["state"].get("path_plan") == {"sender_id": 0, "offsets": []}
            and r["state"].get("map_click_report") == {"x": 0}
            and r["state"].get("custom_bytes") == {"data_hex": ""}
            and r["state"].get("units", {}).get("total_damage_enemy") == 0
            and r["state"].get("economy") == {"encryption_level": 0})
        self.assertNotIn("robot_bullets", report["state"]["units"])
        self.assertNotIn("total_damage_ally", report["state"]["units"])
        # Changing an established identity clears even a fresh zero-valued core report.
        publish(rig.stream, "RobotStaticStatus", message("RobotStaticStatus", {"robot_id": 3}, True))
        report = rig.wait(lambda r: r.get("state", {}).get("self_config", {}).get("robot_id") == 3)
        self.assertNotIn("tech_core", report["state"])
        self.assertNotIn("special_mechanisms", report["state"])
        self.assertNotIn("injury", report["state"])
        self.assertNotIn("path_plan", report["state"])
        self.assertNotIn("map_click_report", report["state"])
        self.assertNotIn("custom_bytes", report["state"])
        self.assertNotIn("units", report["state"])
        self.assertNotIn("economy", report["state"])
        # Repopulate the new blocks so disconnect clearing is observable, independent of expiry.
        for topic in ("RuneStatusSync", "SentryStatusSync", "DartSelectTargetStatusSync", "TechCoreMotionStateSync",
                "GlobalSpecialMechanism", "RobotInjuryStat", "RobotPathPlanInfo", "MapClickInfo", "CustomByteBlock",
                "GlobalUnitStatus", "GlobalLogisticsStatus"):
            publish(rig.stream, topic, message(topic, fixtures[topic]))
        rig.wait(lambda r: all(k in r.get("state", {}) for k in ("rune", "sentry_status", "dart_status", "tech_core",
            "special_mechanisms", "injury", "path_plan", "map_click_report", "custom_bytes", "units", "economy")))
        rig.stream.shutdown(socket.SHUT_RDWR)
        rig.stream.close()
        rig.stream = None
        report = rig.wait(lambda r: r.get("link") == "offline" and not r["hud"]["fresh"])
        self.assertEqual(report["reports"], {})
        self.assertEqual(report["hud"]["buffer_energy"], "--")
        self.assertEqual(report["hud"]["heat"], "--")
        for key in ("rune", "sentry_status", "dart_status", "tech_core", "special_mechanisms", "injury", "path_plan", "map_click_report", "custom_bytes", "units", "economy"):
            self.assertNotIn(key, report["state"])
        self.assertTrue(report["state"]["control_constraints"]["penalty_release_unverified"])
        rig.accept()
        rig.subscriptions()
        rig.wait(lambda r: r.get("link") == "subscribed")
        publish(rig.stream, "RobotStaticStatus", message("RobotStaticStatus", {"robot_id": 3, "max_health": 400}, True))
        publish(rig.stream, "RobotDynamicStatus", message("RobotDynamicStatus", {"current_health": 77}, True))
        report = rig.wait(lambda r: r["hud"]["hp"] == "77 / 400")
        for key in ("rune", "sentry_status", "dart_status", "tech_core", "special_mechanisms", "injury", "path_plan", "map_click_report", "custom_bytes", "units", "economy"):
            self.assertNotIn(key, report["state"])
        self.assertTrue(report["state"]["control_constraints"]["penalty_release_unverified"])
        self.assertEqual(report["gui_stalls"], gui_stalls)
        # The normal UI/backend never emits control PUBLISH during this stage.
        rig.stream.settimeout(0.2)
        while True:
            try:
                header, body = read_packet(rig.stream)
            except socket.timeout:
                break
            self.assertIn(header, (0x40, 0xc0, 0xe0))

    def test_refused_or_withheld_subscription_never_ready(self):
        for grant in (128, 2, None):
            with self.subTest(grant=grant):
                rig = Rig(3200)
                try:
                    rig.accept()
                    if grant is None:
                        self.assertEqual(read_packet(rig.stream)[0], 0x82)
                    else:
                        rig.subscriptions(first_grant=grant)
                    reports = []
                    until = time.monotonic() + 2.6
                    while time.monotonic() < until:
                        reports.append(rig.wait(lambda r: True))
                    self.assertTrue(all(r.get("link") != "subscribed" for r in reports))
                    self.assertTrue(all(not r["hud"]["fresh"] and not r["hud"]["armed"] for r in reports))
                    # Diagnostics keep ticking during the withheld SUBACK; compare
                    # after QML startup rather than counting initial font loading.
                    self.assertGreater(len(reports), 20)
                    self.assertEqual(reports[-1]["gui_stalls"], reports[len(reports)//2]["gui_stalls"])
                finally:
                    rig.close()

    def test_cli_requires_explicit_identity_and_separates_test_source(self):
        base = ["--protocol", "rm2026-v2.0.0", "--mqtt-host", "127.0.0.1",
            "--mqtt-port", "3333", "--robot-id", "3", "--duration-ms", "100"]
        for args in (["--protocol", "rm2026-v2.0.0"], [*base, "--demo"],
            [*base, "--video", "fake.mp4"], [*base, "--robot-id", "03"],
            [*base, "--robot-id", "5"], [*base, "--mqtt-host", "localhost"],
            [*base, "--protocol-test", "--mqtt-host", "192.168.12.1"],
            [*base, "--diagnostics-json"], ["--mqtt-host", "127.0.0.1"],
            [*base, "--hevc-udp"], [*base, "--video-port", "3334"],
            [*base, "--pairing-robot-id", "3"], [*base, "--pairing-confirmed"],
            [*base, "--pairing-region", "japan"],
            [*base, "--hevc-udp", "--video-bind", "127.0.0.1", "--video-source", "127.0.0.1",
                "--video-port", "3334", "--fragment-base", "0", "--pairing-robot-id", "103"],
            [*base, "--hevc-udp", "--video-bind", "127.0.0.1", "--video-source", "127.0.0.1",
                "--video-port", "3334", "--fragment-base", "0", "--protocol-test", "--pairing-robot-id", "3"],
            [*base, "--hevc-udp", "--video-bind", "127.0.0.1", "--video-source", "192.168.12.1",
                "--video-port", "3334", "--fragment-base", "0"]):
            result = subprocess.run([str(EXE), *args], capture_output=True, timeout=5,
                env={**os.environ, "QT_QPA_PLATFORM": PLATFORM})
            self.assertEqual(result.returncode, 2, args)
            self.assertEqual(result.stdout, b"", args)
        for confirmed, phase in ((False, "external_required"), (True, "video_required")):
            with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
                reservation.close()
                args = ["--protocol", "rm2026-v2.0.0", "--hevc-udp", "--video-bind", "127.0.0.1",
                    "--video-source", "127.0.0.1", "--video-port", str(port), "--fragment-base", "0",
                    "--pairing-robot-id", "3", "--pairing-region", "global", "--duration-ms", "250"]
                if confirmed:
                    args.append("--pairing-confirmed")
                result = subprocess.run([str(EXE), *args], capture_output=True, timeout=5,
                    env={**os.environ, "QT_QPA_PLATFORM": PLATFORM, "RM27_ARTIFACT_ROOT": directory,
                        "RM27_SETTINGS_FILE": str(Path(directory) / "settings.ini")})
                self.assertEqual(result.returncode, 0, result.stderr)
                logs = list((Path(directory) / "log").glob("rm27-run-*.log"))
                self.assertEqual(len(logs), 1)
                content = logs[0].read_text(encoding="utf-8")
                self.assertIn("pairing=" + phase, content)
                self.assertNotIn("videoFresh=1", content)
                self.assertNotIn("armed=1", content)

    def test_shutdown_cancels_remaining_subscription_handshake(self):
        rig = Rig(1400)
        self.addCleanup(rig.close)
        rig.accept()
        header, body = read_packet(rig.stream)
        self.assertEqual(header, 0x82)
        # Return a late successful SUBACK after the GUI has requested exit.
        time.sleep(2.2)
        rig.stream.sendall(packet(0x90, body[:2] + b"\x01"))
        header, _ = read_packet(rig.stream)
        self.assertEqual(header, 0xe0) # No second SUBSCRIBE after cancellation.
        self.assertEqual(rig.process.wait(timeout=4), 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    EXE = args.exe.resolve()
    unittest.main(argv=[__file__, *remaining])
