"""Run the C++ executable against a loopback wire peer; only Python stdlib used."""

import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest

from mqtt_wire import packet, read_packet, publish

TOPICS = sorted(("Buff", "CustomByteBlock", "DeployModeStatusSync", "Event", "GameStatus", "GlobalLogisticsStatus",
                 "GlobalUnitStatus", "PenaltyInfo", "RobotDynamicStatus", "RobotModuleStatus",
                 "RobotPosition", "RobotRespawnStatus", "RobotStaticStatus", "RobotPerformanceSelectionSync"))


class NativeChecks(unittest.TestCase):
    def setUp(self):
        self.server = socket.socket()
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(1)
        self.server.settimeout(8)
        self.addCleanup(self.server.close)
        self.lock = threading.Lock()
        self.start_probe()
        self.addCleanup(self.stop)

    def start_probe(self, robot_id=None):
        self.report = {}
        self.expected_id = None if robot_id is None else str(robot_id).encode()
        self.client_id = None
        options = [] if robot_id is None else ["--robot-id", str(robot_id)]
        self.process = subprocess.Popen(
            [str(EXE), "--protocol", "rm2026-v2.0.0", "--port", str(self.server.getsockname()[1]),
             "--duration-ms", "25000"] + options, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        self.reader = threading.Thread(target=self.read_reports, daemon=True)
        self.reader.start()

    def read_reports(self):
        for line in self.process.stdout:
            with self.lock:
                self.report = json.loads(line)

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()  # Only this test's own known subprocess.
        self.process.wait(timeout=3)
        self.reader.join(timeout=3)
        self.process.stdout.close()
        self.process.stderr.close()

    def wait(self, predicate):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            with self.lock:
                status = self.report
            if predicate(status):
                return status
            if self.process.poll() is not None:
                self.fail("native process exited: " + self.process.stderr.read())
            time.sleep(0.01)
        self.fail("native state timeout: " + str(status))

    def connect_peer(self, grants=None, refuse=False, withhold=False):
        stream, _ = self.server.accept()
        stream.settimeout(4)
        self.addCleanup(stream.close)
        header, body = read_packet(stream)
        self.assertEqual(header, 0x10)
        self.assertEqual(body[:10], b"\x00\x04MQTT\x04\x02\x00\x0a")
        client_id = body[12:]
        self.assertEqual(struct.unpack("!H", body[10:12])[0], len(client_id))
        if self.expected_id is None:
            self.assertTrue(client_id.startswith(b"rm2026-local-observer-"))
        else:
            self.assertEqual(client_id, self.expected_id)
        if self.client_id is not None:
            self.assertEqual(client_id, self.client_id)  # Reconnect retains the requested identity.
        self.client_id = client_id
        stream.sendall(b"\x20")
        stream.sendall(b"\x02\x00" + (b"\x05" if refuse else b"\x00"))
        if refuse:
            return stream
        for index, topic in enumerate(TOPICS):
            header, body = read_packet(stream)
            self.assertEqual(header, 0x82)
            self.assertEqual(body[2:], struct.pack("!H", len(topic)) + topic.encode() + b"\x01")
            if withhold:
                return stream, body[:2]
            if index == 0:
                publish(stream, "GameStatus", b"\x28\x04", mid=600)
                self.assertEqual(read_packet(stream), (0x40, b"\x02\x58"))
            grant = grants[index] if grants is not None else 1
            stream.sendall(packet(0x90, body[:2] + bytes([grant])))
            if grant not in (0, 1):
                self.wait(lambda state: state.get("link") == "subscription-rejected")
                return stream
        self.wait(lambda state: state.get("link") == "subscribed")
        return stream

    def test_all_topics_and_invalid_inputs(self):
        stream = self.connect_peer()
        payloads = {
            "Buff": b"\x10\x02", "RobotPerformanceSelectionSync": b"\x18\x00", "DeployModeStatusSync": b"\x08\x00",
            "CustomByteBlock": b"\x0a\x01\xff", "Event": b"\x08\x01\x12\x02hi",
            "GameStatus": b"\x28\x04\x30\xa4\x03", "GlobalLogisticsStatus": b"\x08\x64",
            "GlobalUnitStatus": b"\x5a\x0a" + bytes(range(10)), "PenaltyInfo": b"\x08\x01",
            "RobotDynamicStatus": b"\x08\x00", "RobotModuleStatus": b"\x08\x01",
            "RobotPosition": b"\x0d" + struct.pack("<f", 2.5),
            "RobotRespawnStatus": b"\x08\x00", "RobotStaticStatus": b"\x20\x65"}
        # Identity first: a changed/missing identity clears prior cached samples.
        ordered = ["RobotStaticStatus"] + [topic for topic in TOPICS if topic != "RobotStaticStatus"]
        for mid, topic in enumerate(ordered, 1):
            publish(stream, topic, payloads[topic], mid=mid)
            self.assertEqual(read_packet(stream), (0x40, struct.pack("!H", mid)))
        report = self.wait(lambda state: state.get("counts", {}).get("accepted") == len(TOPICS))
        self.assertEqual(report["fresh"]["RobotDynamicStatus"], {"current_health": 0})
        self.assertEqual(report["fresh"]["GlobalUnitStatus"]["robot_health"], list(range(10)))
        self.assertEqual(report["fresh"]["CustomByteBlock"], {"data": "/w=="})
        self.assertEqual(report["state"]["game"], {"stage_code": 4, "phase": "playing", "countdown_sec": 420})
        self.assertEqual(report["state"]["penalty"], {"type_code": 1, "kind": "yellow_card"})
        self.assertEqual(report["state"]["latest_buff"], {"type_code": 2, "kind": "defense_or_vulnerability"})
        self.assertEqual(report["state"]["performance"], {"sentry_control": 0})
        self.assertEqual(report["state"]["deployment"], {"code": 0, "state": "not_deployed"})
        self.assertEqual(report["state"]["self_config"], {"robot_id": 101, "team": "blue"})
        self.assertEqual(report["state"]["self_dynamic"], {"health": 0})
        self.assertEqual(report["state"]["modules"], {"power_manager": {"code": 1, "state": "online"}})
        self.assertIs(report["state"]["respawn"]["pending"], False)
        self.assertEqual(report["state"]["latest_event"], {"id": 1, "kind": "kill", "param": "hi"})
        self.assertEqual(report["state"]["position"], {"x": 2.5})
        self.assertEqual(report["state"]["units"]["ally"]["robots"],
                         [{"role_id": role, "health": hp} for role, hp in zip((1, 2, 3, 4, 7), range(5))])
        self.assertEqual(report["state"]["units"]["enemy"]["robots"],
                         [{"role_id": role, "health": hp} for role, hp in zip((1, 2, 3, 4, 7), range(5, 10))])
        before = report["counts"]["rejected"]
        invalid = [("GameStatus", b"\x28"), ("GameStatus", b""),
                   ("GameStatus", b"\xf8\x07\x01"), ("RadarInfoToClient", b"\x08\x01"),
                   ("CommonCommand", b"\x08\x01"), ("rm27/sim/v1/referee", b"{}"),
                   ("GlobalUnitStatus", b"\x5a\x02\x01\x02"),
                   ("RobotPosition", b"\x0d" + struct.pack("<f", float("nan"))),
                   ("CustomByteBlock", b"\x0a\xad\x02" + b"x" * 301)]
        for mid, (topic, payload) in enumerate(invalid, 100):
            publish(stream, topic, payload, mid=mid)
            self.assertEqual(read_packet(stream), (0x40, struct.pack("!H", mid)))
        self.wait(lambda state: state.get("counts", {}).get("rejected") == before + len(invalid))

    def test_duplicate_retained_and_independent_expiry(self):
        stream = self.connect_peer()
        publish(stream, "GameStatus", b"\x28\x04", mid=7)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.wait(lambda state: "GameStatus" in state.get("fresh", {}))
        time.sleep(1.0)
        publish(stream, "GameStatus", b"\x28\x04", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        publish(stream, "RobotDynamicStatus", b"\x08\x00")
        self.wait(lambda state: "RobotDynamicStatus" in state.get("fresh", {}))
        report = self.wait(lambda state: "GameStatus" in state.get("stale", []))
        self.assertIn("RobotDynamicStatus", report["fresh"])
        self.assertNotIn("game", report["state"])
        self.assertEqual(report["counts"]["duplicates"], 1)
        publish(stream, "GameStatus", b"\x28\x04", mid=8, retained=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x08"))
        report = self.wait(lambda state: state.get("counts", {}).get("retained") == 1)
        self.assertNotIn("GameStatus", report["fresh"])
        self.assertNotIn("game", report["state"])
        publish(stream, "GameStatus", b"\x28\x04", mid=7)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.wait(lambda state: "GameStatus" in state.get("fresh", {}))

    def test_disconnect_and_reconnect(self):
        stream = self.connect_peer()
        publish(stream, "GameStatus", b"\x28\x04", mid=7)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.wait(lambda state: "GameStatus" in state.get("fresh", {}))
        publish(stream, "RobotDynamicStatus", b"\x08\x00")
        self.wait(lambda state: state.get("state", {}).get("self_dynamic") == {"health": 0})
        self.send_mapped(stream, "RobotModuleStatus", b"\x40\x01")
        self.send_mapped(stream, "RobotRespawnStatus", b"\x30\x01")
        self.send_mapped(stream, "Event", b"\x08\x07")
        self.send_mapped(stream, "RobotPosition", b"\x25" + struct.pack("<f", 0))
        self.send_mapped(stream, "Buff", b"\x10\x01")
        self.send_mapped(stream, "RobotPerformanceSelectionSync", b"\x08\x01")
        self.send_mapped(stream, "DeployModeStatusSync", b"\x08\x01")
        stream.shutdown(socket.SHUT_RDWR)
        stream.close()
        report = self.wait(lambda state: state.get("link") == "offline")
        self.assertEqual(report["fresh"], {})
        self.assertEqual(report["stale"], [])
        self.assertEqual(self.reported_state(report), {})
        stream = self.connect_peer(grants=[0] * len(TOPICS))
        report = self.wait(lambda state: state.get("sessions") == 2 and state.get("link") == "subscribed")
        self.assertEqual(report["fresh"], {})
        self.assertEqual(self.reported_state(report), {})
        publish(stream, "GameStatus", b"\x28\x04", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        report = self.wait(lambda state: state.get("counts", {}).get("duplicates") == 1)
        self.assertEqual(report["fresh"], {})

    def test_rejected_subscription_and_late_suback(self):
        for grant in (0x80, 2):
            stream = self.connect_peer(grants=[grant])
            before = self.report["counts"]["rejected"]
            publish(stream, "GameStatus", b"\x28\x04", mid=7)
            self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
            self.wait(lambda state: state.get("counts", {}).get("rejected") == before + 1)
            self.assertEqual(self.report["fresh"], {})
            stream.shutdown(socket.SHUT_RDWR)
            stream.close()
            self.wait(lambda state: state.get("link") == "offline")
        stream, mid = self.connect_peer(withhold=True)
        self.wait(lambda state: state.get("link") == "subscription-timeout")
        before = self.report["counts"]["rejected"]
        stream.sendall(packet(0x90, mid + b"\x01"))
        publish(stream, "GameStatus", b"\x28\x04", mid=9)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x09"))
        self.wait(lambda state: state.get("counts", {}).get("rejected") == before + 1)
        self.assertEqual(self.report["link"], "subscription-timeout")
        self.assertEqual(self.report["fresh"], {})

    def test_connack_refusal_and_cli_boundaries(self):
        stream = self.connect_peer(refuse=True)
        self.wait(lambda state: state.get("link") == "offline")
        stream.close()
        self.connect_peer()
        for options in (["--host", "192.168.12.1"], ["--host", "localhost"],
                        ["--port", "0"], ["--protocol", "rm2027"]):
            result = subprocess.run([str(EXE), "--protocol", "rm2026-v2.0.0"] + options,
                                    capture_output=True, text=True, encoding="utf-8", timeout=5)
            self.assertEqual(result.returncode, 2, result.stderr)

    def test_numeric_identity_reconnect_and_no_publish(self):
        for robot_id in (3, 103):
            with self.subTest(robot_id=robot_id):
                self.stop()
                self.start_probe(robot_id)
                stream = self.connect_peer(refuse=True)
                self.wait(lambda state: state.get("link") == "offline")
                stream.close()
                stream = self.connect_peer()
                report = self.wait(lambda state: state.get("link") == "subscribed")
                self.assertEqual(report["client_id"], str(robot_id))
                self.assertEqual(self.reported_state(report), {})  # Requested ID is not a server identity report.
                self.send_mapped(stream, "GameStatus", b"\x28\x04")
                stream.shutdown(socket.SHUT_RDWR)
                stream.close()
                self.wait(lambda state: state.get("link") == "offline")
                stream = self.connect_peer(grants=[0] * len(TOPICS))
                report = self.wait(lambda state: state.get("sessions") == 2)
                self.assertEqual(self.reported_state(report), {})
                stream.settimeout(0.3)
                with self.assertRaises(socket.timeout):
                    read_packet(stream)  # CONNECT/SUBSCRIBE/PUBACK only; no PUBLISH/control.
                stream.close()

    def test_invalid_numeric_identity(self):
        for robot_id in ("0", "5", "8", "100", "105", "108", "-3", "+3", "03", "R3", "4294967296", ""):
            with self.subTest(robot_id=robot_id):
                result = subprocess.run([str(EXE), "--protocol", "rm2026-v2.0.0", "--robot-id", robot_id],
                                        capture_output=True, text=True, encoding="utf-8", timeout=5)
                self.assertEqual(result.returncode, 2, result.stderr)

    def test_bounded_run_exits_cleanly(self):
        # Let an owned second probe exit through QCoreApplication and its RAII
        # destructor, rather than relying only on forced test cleanup.
        result = subprocess.run([str(EXE), "--protocol", "rm2026-v2.0.0", "--port",
                                 str(self.server.getsockname()[1]), "--duration-ms", "150"],
                                capture_output=True, text=True, encoding="utf-8", timeout=4)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout.splitlines()[0])["local_only"])

    def reported_state(self, report):
        # Shared JSON includes the local constraint; this read-only observer grants no authority.
        self.assertEqual(report["state"]["control_constraints"], {"penalty_release_unverified": False})
        return {key: value for key, value in report["state"].items() if key != "control_constraints"}

    def send_mapped(self, stream, topic, payload):
        before = self.report["counts"]["accepted"]
        publish(stream, topic, payload, mid=7)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        return self.reported_state(self.wait(lambda state: state.get("counts", {}).get("accepted") == before + 1))

    def test_buff_performance_deploy_presence_and_expiry(self):
        stream = self.connect_peer()
        self.send_mapped(stream, "RobotStaticStatus", b"\x20\x65")
        kinds = ("attack", "defense_or_vulnerability", "cooling", "chassis_power",
                 "recovery", "exchangeable_shots", "terrain_prewarning")
        for code, kind in enumerate(kinds, 1):
            buff = self.send_mapped(stream, "Buff", bytes([0x10, code]))["latest_buff"]
            self.assertEqual(buff, {"type_code": code, "kind": kind})  # No self-ID fallback.
        buff = self.send_mapped(stream, "Buff", bytes.fromhex(
            "08 00 10 02 18 ff ff ff ff ff ff ff ff ff 01 20 00 28 00"))["latest_buff"]
        self.assertEqual(buff, {"reported_robot_id": 0, "type_code": 2, "kind": "defense_or_vulnerability",
                               "level": -1, "max_time": 0, "left_time": 0})
        buff = self.send_mapped(stream, "Buff", bytes.fromhex(
            "08 ff ff ff ff 0f 18 80 80 80 80 f8 ff ff ff ff 01 28 ff ff ff ff 0f"))["latest_buff"]
        self.assertEqual(buff, {"reported_robot_id": 4294967295, "kind": "unknown",
                               "level": -2147483648, "left_time": 4294967295})
        for code in (0, 99):
            self.assertEqual(self.send_mapped(stream, "Buff", bytes([0x10, code, 0x30, 1]))["latest_buff"],
                             {"type_code": code, "kind": "unknown"})  # Unknown field 6 stays opaque.
        performance = self.send_mapped(stream, "RobotPerformanceSelectionSync",
            bytes.fromhex("08 00 10 ff ff ff ff 0f 18 00"))["performance"]
        self.assertEqual(performance, {"shooter": 0, "chassis": 4294967295, "sentry_control": 0})
        self.assertEqual(self.send_mapped(stream, "RobotPerformanceSelectionSync", b"\x18\x63")["performance"],
                         {"sentry_control": 99})
        for code, name in ((0, "not_deployed"), (1, "deployed"), (99, "unknown")):
            self.assertEqual(self.send_mapped(stream, "DeployModeStatusSync", bytes([8, code]))["deployment"],
                             {"code": code, "state": name})

        # Fresh independent samples; invalid/retained/DUP arrivals must not extend them.
        for topic, payload in (("Buff", b"\x10\x02"), ("RobotPerformanceSelectionSync", b"\x08\x00"),
                               ("DeployModeStatusSync", b"\x08\x01")):
            self.send_mapped(stream, topic, payload)
        time.sleep(1.0)
        before = self.report["counts"]["rejected"]
        for topic in ("Buff", "RobotPerformanceSelectionSync", "DeployModeStatusSync"):
            for payload, options in ((b"", {}), (b"\x08", {}), (b"\x30\x01", {}),
                                     (b"\x08\x00", {"duplicate": True}),
                                     (b"\x08\x00", {"retained": True})):
                publish(stream, topic, payload, mid=7, **options)
                self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.send_mapped(stream, "GameStatus", b"\x28\x04")
        report = self.wait(lambda state: all(topic in state.get("stale", []) for topic in
            ("Buff", "RobotPerformanceSelectionSync", "DeployModeStatusSync")))
        self.assertEqual(report["counts"]["rejected"], before + 9)
        self.assertEqual(report["counts"]["duplicates"], 3)
        self.assertEqual(report["counts"]["retained"], 3)
        self.assertNotIn("latest_buff", report["state"])
        self.assertNotIn("performance", report["state"])
        self.assertNotIn("deployment", report["state"])
        self.assertIn("game", report["state"])
        self.send_mapped(stream, "Buff", b"\x10\x01")
        self.send_mapped(stream, "RobotPerformanceSelectionSync", b"\x10\x01")
        self.send_mapped(stream, "DeployModeStatusSync", b"\x08\x01")
        self.assertEqual(self.send_mapped(stream, "RobotStaticStatus", b"\x20\x01"),
                         {"self_config": {"robot_id": 1, "team": "red"}})

    def test_game_phases_presence_and_settlement(self):
        stream = self.connect_peer()
        phases = ("not_started", "preparation", "self_check", "countdown", "playing", "settlement")
        for code, phase in enumerate(phases):
            game = self.send_mapped(stream, "GameStatus", b"\x28" + bytes([code]) + b"\x48\x01\x50\x01")["game"]
            expected = {"stage_code": code, "phase": phase, "reported_result": 1, "reported_end_reason": 1}
            if code == 5:
                expected.update(result=1, end_reason=1)
            self.assertEqual(game, expected)
        payload = (b"\x08\x01\x10\x03\x18\x00\x20\x00\x28\x04\x30" +
                   b"\xff" * 9 + b"\x01\x38\x00\x40\x00\x48\xff\x01\x50\xff\x01")
        game = self.send_mapped(stream, "GameStatus", payload)["game"]
        self.assertEqual(game, {"current_round": 1, "total_rounds": 3, "red_score": 0, "blue_score": 0,
                               "stage_code": 4, "phase": "playing", "countdown_sec": -1,
                               "elapsed_sec": 0, "paused": False, "reported_result": 255, "reported_end_reason": 255})
        self.assertIs(game["paused"], False)  # Not integer zero in JSON.
        game = self.send_mapped(stream, "GameStatus", b"\x28\x63\x48\x00\x50\x01")["game"]
        self.assertEqual(game, {"stage_code": 99, "phase": "unknown", "reported_result": 0, "reported_end_reason": 1})
        self.assertEqual(self.send_mapped(stream, "GameStatus", b"\x40\x01")["game"], {"paused": True, "phase": "unknown"})
        self.assertEqual(self.send_mapped(stream, "GameStatus", b"\x28\x05")["game"], {"stage_code": 5, "phase": "settlement"})
        for result in (0, 1, 2, 3, 255):
            encoded = bytes([result]) if result < 128 else b"\xff\x01"
            game = self.send_mapped(stream, "GameStatus", b"\x28\x05\x48" + encoded)["game"]
            self.assertEqual(game["reported_result"], result)
            self.assertEqual(game.get("result"), result if result <= 2 else None)
        for reason in (0, 1, 9, 10, 255):
            encoded = bytes([reason]) if reason < 128 else b"\xff\x01"
            game = self.send_mapped(stream, "GameStatus", b"\x28\x05\x50" + encoded)["game"]
            self.assertEqual(game["reported_end_reason"], reason)
            self.assertEqual(game.get("end_reason"), reason if 1 <= reason <= 9 else None)

    def test_penalty_types_presence_and_expiry_not_release(self):
        stream = self.connect_peer()
        kinds = ("yellow_card", "both_yellow_cards", "red_card", "power", "heat", "fire_rate")
        for code, kind in enumerate(kinds, 1):
            penalty = self.send_mapped(stream, "PenaltyInfo", b"\x08" + bytes([code]) + b"\x10\x1e\x18\x00")["penalty"]
            self.assertEqual(penalty, {"type_code": code, "kind": kind, "effect_sec": 30, "total": 0})
        for code in (0, 99):
            penalty = self.send_mapped(stream, "PenaltyInfo", b"\x08" + bytes([code]))["penalty"]
            self.assertEqual(penalty, {"type_code": code, "kind": "unknown"})
        self.assertEqual(self.send_mapped(stream, "PenaltyInfo", b"\x10\x00")["penalty"], {"effect_sec": 0, "kind": "unknown"})
        self.send_mapped(stream, "PenaltyInfo", b"\x08\x03\x10\x1e")
        before = self.report["counts"]["accepted"]
        publish(stream, "PenaltyInfo", b"\x08\x00", mid=9, retained=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x09"))
        report = self.wait(lambda state: state.get("counts", {}).get("retained") == 1)
        self.assertEqual(report["counts"]["accepted"], before)
        self.assertEqual(report["state"]["penalty"]["kind"], "red_card")
        time.sleep(1.0)
        publish(stream, "PenaltyInfo", b"\x08\x03\x10\x1e", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.send_mapped(stream, "GameStatus", b"\x28\x04\x40\x00")
        report = self.wait(lambda state: "PenaltyInfo" in state.get("stale", []))
        self.assertNotIn("penalty", report["state"])
        self.assertEqual(report["state"]["game"], {"stage_code": 4, "phase": "playing", "paused": False})
        self.assertEqual(report["counts"]["duplicates"], 1)
        # No synthetic zero, cleared flag, elapsed deduction or permission field.
        report = self.send_mapped(stream, "PenaltyInfo", b"\x18\x00")
        self.assertEqual(report["penalty"], {"total": 0, "kind": "unknown"})
        publish(stream, "RobotStaticStatus", b"\x20\x01")
        report = self.wait(lambda state: state.get("state", {}).get("self_config", {}).get("robot_id") == 1)
        self.assertEqual(self.reported_state(report), {"self_config": {"robot_id": 1, "team": "red"}})

    def test_module_codes_presence_and_independent_expiry(self):
        stream = self.connect_peer()
        names = ("power_manager", "rfid", "light_strip", "small_shooter", "big_shooter", "uwb",
                 "armor", "video_transmission", "capacitor", "main_controller", "laser_detection_module")
        for code, state in ((0, "offline"), (1, "online"), (2, "installation_offline"), (99, "unknown")):
            payload = b"".join(bytes([tag << 3, code]) for tag in range(1, 12))
            modules = self.send_mapped(stream, "RobotModuleStatus", payload)["modules"]
            self.assertEqual(modules, {name: {"code": code, "state": state} for name in names})
        self.assertEqual(self.send_mapped(stream, "RobotModuleStatus", b"\x10\x00")["modules"],
                         {"rfid": {"code": 0, "state": "offline"}})
        # Unknown codes are kept, not treated as truthy/online or filled into missing modules.
        self.assertEqual(self.send_mapped(stream, "RobotModuleStatus", b"\x40\xff\x01")["modules"],
                         {"video_transmission": {"code": 255, "state": "unknown"}})
        time.sleep(1.0)
        before = self.report["counts"]["accepted"]
        publish(stream, "RobotModuleStatus", b"\x40\x01", mid=9, retained=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x09"))
        report = self.wait(lambda state: state.get("counts", {}).get("retained") == 1)
        self.assertEqual(report["counts"]["accepted"], before)
        self.assertEqual(report["state"]["modules"]["video_transmission"]["code"], 255)
        publish(stream, "RobotModuleStatus", b"\x40\x01", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.send_mapped(stream, "RobotRespawnStatus", b"\x08\x00")
        report = self.wait(lambda state: "RobotModuleStatus" in state.get("stale", []))
        self.assertNotIn("modules", report["state"])
        self.assertEqual(report["state"]["respawn"], {"pending": False})
        self.assertEqual(report["counts"]["duplicates"], 1)

    def test_respawn_presence_and_expiry_not_permission(self):
        stream = self.connect_peer()
        for flag in (0, 1):
            payload = bytes([8, flag, 16, 0, 24, 0, 32, flag, 40, 0, 48, flag])
            respawn = self.send_mapped(stream, "RobotRespawnStatus", payload)["respawn"]
            self.assertEqual(respawn, {"pending": bool(flag), "required_progress": 0, "current_progress": 0,
                                      "reported_can_free": bool(flag), "gold_cost": 0, "reported_can_pay": bool(flag)})
            for key in ("pending", "reported_can_free", "reported_can_pay"):
                self.assertIs(respawn[key], bool(flag))
        # Progress is not seconds/percent; no clamping, ratio or inferred life/permission.
        self.assertEqual(self.send_mapped(stream, "RobotRespawnStatus", b"\x10\x0a\x18\x14\x20\x00")["respawn"],
                         {"required_progress": 10, "current_progress": 20, "reported_can_free": False})
        self.assertEqual(self.send_mapped(stream, "RobotRespawnStatus", b"\x28\xff\xff\xff\xff\x0f")["respawn"],
                         {"gold_cost": 4294967295})
        self.assertEqual(self.send_mapped(stream, "RobotRespawnStatus", b"\x30\x00")["respawn"],
                         {"reported_can_pay": False})
        time.sleep(1.0)
        publish(stream, "RobotRespawnStatus", b"\x30\x01", mid=9, retained=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x09"))
        self.wait(lambda state: state.get("counts", {}).get("retained") == 1)
        publish(stream, "RobotRespawnStatus", b"\x30\x01", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.send_mapped(stream, "RobotModuleStatus", b"\x40\x01")
        report = self.wait(lambda state: "RobotRespawnStatus" in state.get("stale", []))
        self.assertNotIn("respawn", report["state"])
        self.assertEqual(report["state"]["modules"], {"video_transmission": {"code": 1, "state": "online"}})
        self.assertEqual(report["counts"]["duplicates"], 1)
        self.send_mapped(stream, "RobotRespawnStatus", b"\x08\x01\x20\x01\x30\x01")
        report = self.send_mapped(stream, "RobotStaticStatus", b"\x20\x01")
        self.assertEqual(report, {"self_config": {"robot_id": 1, "team": "red"}})

    def test_event_kinds_raw_parameters_and_expiry(self):
        stream = self.connect_peer()
        kinds = ("kill", "outpost_destroyed", "large_energy_result", "energy_activated",
                 "ally_sniper_damage", "enemy_sniper_damage", "enemy_air_support", "enemy_air_support_countered",
                 "dart_hit", "enemy_dart_gate_opened", "base_attacked", "enemy_outpost_stopped",
                 "enemy_base_armor_opened", "enemy_assembly_forced_exit", "assembly_result")
        for event_id, kind in enumerate(kinds, 1):
            event = self.send_mapped(stream, "Event", bytes([8, event_id]) + b"\x12\x051,101")["latest_event"]
            self.assertEqual(event, {"id": event_id, "kind": kind, "param": "1,101"})
        for encoded, value in ((b"\x00", 0), (b"\x10", 16), (b"\xff" * 9 + b"\x01", -1)):
            self.assertEqual(self.send_mapped(stream, "Event", b"\x08" + encoded)["latest_event"],
                             {"id": value, "kind": "unknown"})
        raw = "飞镖命中\x00原始参数".encode("utf-8")
        self.assertEqual(self.send_mapped(stream, "Event", bytes([18, len(raw)]) + raw)["latest_event"],
                         {"kind": "unknown", "param": raw.decode("utf-8")})
        self.assertEqual(self.send_mapped(stream, "Event", b"\x12\x00")["latest_event"],
                         {"kind": "unknown", "param": ""})
        self.assertEqual(self.send_mapped(stream, "Event", b"\x08\x07")["latest_event"],
                         {"id": 7, "kind": "enemy_air_support"})
        # Malformed gameplay parameter stays raw; no derived kill/HP/map changes.
        expected = {"id": 1, "kind": "kill", "param": "1,x,101"}
        state = self.send_mapped(stream, "Event", b"\x08\x01\x12\x071,x,101")
        self.assertEqual(state, {"latest_event": expected})
        before = self.report["counts"]["rejected"]
        publish(stream, "Event", b"\x12\x01\xff", mid=9)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x09"))
        report = self.wait(lambda state: state.get("counts", {}).get("rejected") == before + 1)
        self.assertEqual(report["state"]["latest_event"], expected)
        time.sleep(1.0)
        publish(stream, "Event", b"\x08\x09", mid=9, retained=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x09"))
        self.wait(lambda state: state.get("counts", {}).get("retained") == 1)
        publish(stream, "Event", b"\x08\x09", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.send_mapped(stream, "RobotPosition", b"\x0d" + struct.pack("<f", 0))
        report = self.wait(lambda state: "Event" in state.get("stale", []))
        self.assertNotIn("latest_event", report["state"])
        self.assertEqual(report["state"]["position"], {"x": 0})
        self.assertEqual(report["counts"]["duplicates"], 1)

    def test_position_presence_nonfinite_and_no_map_inference(self):
        stream = self.connect_peer()
        self.send_mapped(stream, "RobotStaticStatus", b"\x20\x01")
        payload = (b"\x0d" + struct.pack("<f", 0) + b"\x15" + struct.pack("<f", -2.5) +
                   b"\x1d" + struct.pack("<f", 3) + b"\x25" + struct.pack("<f", 0) + b"\x28\x6a")
        position = self.send_mapped(stream, "RobotPosition", payload)["position"]
        self.assertEqual(position, {"x": 0, "y": -2.5, "z": 3, "yaw_deg": 0, "reported_robot_id": 106})
        self.assertEqual(self.send_mapped(stream, "RobotPosition", b"\x0d" + struct.pack("<f", 1.25))["position"],
                         {"x": 1.25})  # No inherited axes/ID or static-ID fallback.
        for yaw in (-540, 360, 721):
            self.assertEqual(self.send_mapped(stream, "RobotPosition", b"\x25" + struct.pack("<f", yaw))["position"],
                             {"yaw_deg": yaw})
        for robot_id in (0, 6, 7, 106, 107, 99):
            self.assertEqual(self.send_mapped(stream, "RobotPosition", bytes([40, robot_id]))["position"],
                             {"reported_robot_id": robot_id})
        self.send_mapped(stream, "RobotPosition", payload)
        before = self.report["counts"]["rejected"]
        invalid = [(tag, number) for tag in (13, 21, 29, 37)
                   for number in (float("nan"), float("inf"), -float("inf"))]
        for index, (tag, number) in enumerate(invalid, 1):
            publish(stream, "RobotPosition", bytes([tag]) + struct.pack("<f", number), mid=9)
            self.assertEqual(read_packet(stream), (0x40, b"\x00\x09"))
            report = self.wait(lambda state: state.get("counts", {}).get("rejected") == before + index)
            self.assertEqual(report["state"]["position"], position)
        publish(stream, "RobotPosition", b"\x28\x07", mid=9, retained=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x09"))
        self.wait(lambda state: state.get("counts", {}).get("retained") == 1)
        publish(stream, "RobotPosition", b"\x28\x07", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.send_mapped(stream, "Event", b"\x08\x0b")
        report = self.wait(lambda state: "RobotPosition" in state.get("stale", []))
        self.assertNotIn("position", report["state"])
        self.assertEqual(report["state"]["latest_event"], {"id": 11, "kind": "base_attacked"})
        self.assertEqual(report["counts"]["duplicates"], 1)
        self.send_mapped(stream, "RobotPosition", payload)
        report = self.send_mapped(stream, "RobotStaticStatus", b"\x20\x65")
        self.assertEqual(report, {"self_config": {"robot_id": 101, "team": "blue"}})

    def test_domain_presence_expiry_and_identity_change(self):
        stream = self.connect_peer()
        publish(stream, "RobotStaticStatus", b"\x20\x6b\x40\x03\x48\xe8\x07\x5d" + struct.pack("<f", 1.25))
        self.wait(lambda state: state.get("state", {}).get("self_config", {}).get("robot_id") == 107)
        publish(stream, "GlobalUnitStatus", b"\x08\x00\x20\x00\x28\x03\x30\x14\x50\x00\x5a\x0a" + bytes(range(10, 20)))
        dynamic = b"\x08\x00\x15" + struct.pack("<f", 0) + b"\x1d" + struct.pack("<f", 24) + b"\x48\x50\x28\x00\x58\x00"
        publish(stream, "RobotDynamicStatus", dynamic)
        publish(stream, "GlobalLogisticsStatus", b"\x08\x00\x10" + b"\xff"*9 + b"\x01")
        report = self.wait(lambda state: "economy" in state.get("state", {}))["state"]
        self.assertEqual(report["self_config"], {"robot_id": 107, "team": "blue", "level": 3, "max_health": 1000, "heat_cooldown_rate": 1.25})
        self.assertEqual(report["self_dynamic"], {"health": 0, "heat": 0, "last_projectile_fire_rate": 24,
            "permitted_shots": 80, "buffer_energy": 0, "out_of_combat_countdown": 0})
        self.assertEqual(report["economy"], {"remaining": 0, "total_obtained": "18446744073709551615"})
        self.assertEqual(report["units"]["ally"]["base"], {"health": 0})
        self.assertEqual(report["units"]["enemy"]["base"], {"health": 20})
        self.assertEqual(report["units"]["ally"]["outpost"]["life"], "destroyed")
        self.assertEqual(report["units"]["enemy"]["outpost"]["life"], "alive")
        self.assertEqual(report["units"]["ally"]["robots"][0], {"role_id": 1, "health": 10})
        self.assertEqual(report["units"]["enemy"]["robots"][0], {"role_id": 1, "health": 15})
        time.sleep(1.0)
        publish(stream, "RobotDynamicStatus", b"\x48\x00")
        self.wait(lambda state: state.get("state", {}).get("self_dynamic") == {"permitted_shots": 0})
        report = self.wait(lambda state: "RobotStaticStatus" in state.get("stale", []) and
                           set(state.get("state", {})) == {"self_dynamic", "control_constraints"})
        self.assertEqual(report["state"]["self_dynamic"], {"permitted_shots": 0})
        publish(stream, "RobotStaticStatus", b"\x20\x01")
        report = self.wait(lambda state: state.get("state", {}).get("self_config", {}).get("robot_id") == 1)
        self.assertEqual(self.reported_state(report), {"self_config": {"robot_id": 1, "team": "red"}})
        self.assertEqual(set(report["fresh"]), {"RobotStaticStatus"})
        publish(stream, "RobotStaticStatus", b"\x48\x64")  # Missing ID must not inherit red.
        self.wait(lambda state: state.get("state", {}).get("self_config") == {"max_health": 100, "team": "unknown"})
        for robot_id, team in ((5, "red"), (105, "blue"), (106, "blue"), (99, "unknown"), (0, "unknown")):
            publish(stream, "RobotStaticStatus", b"\x20" + bytes([robot_id]))
            self.wait(lambda state: state.get("state", {}).get("self_config") == {"robot_id": robot_id, "team": team})

    def test_outpost_states_and_conflicting_values(self):
        stream = self.connect_peer()
        publish(stream, "GlobalUnitStatus", b"\x08\x64\x5a\x0a" + bytes(range(10)))
        self.wait(lambda state: state.get("state", {}).get("units", {}).get("ally", {}).get("base") == {"health": 100})
        for status in range(6):
            publish(stream, "GlobalUnitStatus", b"\x28" + bytes([status]))
            life = "alive" if status <= 2 else "destroyed"
            report = self.wait(lambda state: state.get("state", {}).get("units", {}).get("ally", {}).get("outpost", {}).get("status") == status)
            self.assertEqual(report["state"]["units"]["ally"]["outpost"]["life"], life)
            self.assertEqual(report["state"]["units"]["enemy"]["outpost"]["life"], "unknown")
            self.assertEqual(report["state"]["units"]["ally"]["base"], {})
            self.assertTrue(all("health" not in robot for robot in report["state"]["units"]["ally"]["robots"]))
        for payload, expected in ((b"\x20\x00", "destroyed"), (b"\x20\x01", "alive"),
                                  (b"\x20\x00\x28\x01", "unknown"), (b"\x20\x01\x28\x03", "unknown"),
                                  (b"\x20\x01\x28\x63", "unknown")):
            before = self.report["counts"]["accepted"]
            publish(stream, "GlobalUnitStatus", payload)
            report = self.wait(lambda state: state.get("counts", {}).get("accepted") == before + 1)
            self.assertEqual(report["state"]["units"]["ally"]["outpost"]["life"], expected)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    EXE = args.exe.resolve()
    unittest.main(argv=[sys.argv[0]] + remaining)
