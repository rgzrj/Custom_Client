"""Run with --descriptor pointing to protoc's FileDescriptorSet output."""

import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from rm2026_protocol import INBOUND_TOPICS, Protocol, Snapshot, VERSION


class ProtocolChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.protocol = Protocol(DESCRIPTOR)

    def test_all_inbound_topics_and_presence(self):
        for topic in INBOUND_TOPICS:
            self.assertEqual(self.protocol.decode(VERSION, topic, b""), {})
        self.assertEqual(self.protocol.decode(VERSION, "RobotDynamicStatus", b"\x08\x00"),
                         {"current_health": 0})
        self.assertEqual(self.protocol.decode(VERSION, "GameStatus", b"\x28\x04\x30\xa4\x03"),
                         {"current_stage": 4, "stage_countdown_sec": 420})

    def test_reference_field_types_and_numbers(self):
        # Independent assertions against PDF tables, not merely serialize/parse symmetry.
        message = self.protocol.message("GameStatus")
        field = message.DESCRIPTOR.fields_by_name["stage_countdown_sec"]
        self.assertEqual((field.number, field.type, field.has_presence), (6, field.TYPE_INT32, True))
        message = self.protocol.message("RobotDynamicStatus")
        self.assertEqual(message.DESCRIPTOR.fields_by_name["remaining_ammo"].number, 9)
        self.assertEqual(message.DESCRIPTOR.fields_by_name["last_projectile_fire_rate"].type, field.TYPE_FLOAT)
        message = self.protocol.message("GlobalUnitStatus")
        self.assertEqual(message.DESCRIPTOR.fields_by_name["robot_health"].number, 11)
        self.assertEqual(message.DESCRIPTOR.fields_by_name["enemy_outpost_status"].number, 10)
        self.assertEqual(self.protocol.message("RobotPosition").DESCRIPTOR.fields_by_name["robot_id"].number, 5)

    def test_packed_and_unpacked_robot_health(self):
        expected = {"robot_health": list(range(10))}
        self.assertEqual(self.protocol.decode(VERSION, "GlobalUnitStatus", b"\x5a\x0a" + bytes(range(10))), expected)
        unpacked = b"".join(bytes((0x58, value)) for value in range(10))
        self.assertEqual(self.protocol.decode(VERSION, "GlobalUnitStatus", unpacked), expected)
        with self.assertRaises(ValueError):
            self.protocol.decode(VERSION, "GlobalUnitStatus", b"\x5a\x02\x01\x02")

    def test_buff_performance_and_deploy_official_fields(self):
        # PDF 62-63, 69-70: independent field numbers/types and literal wire bytes.
        schemas = {
            "Buff": (("robot_id", 1, 13), ("buff_type", 2, 13), ("buff_level", 3, 5),
                     ("buff_max_time", 4, 13), ("buff_left_time", 5, 13)),
            "RobotPerformanceSelectionSync": (("shooter", 1, 13), ("chassis", 2, 13),
                                              ("sentry_control", 3, 13)),
            "DeployModeStatusSync": (("status", 1, 13),),
        }
        for topic, expected in schemas.items():
            descriptor = self.protocol.message(topic).DESCRIPTOR
            self.assertEqual(tuple((f.name, f.number, f.type) for f in descriptor.fields), expected)
            self.assertTrue(all(f.has_presence for f in descriptor.fields))
            self.assertEqual(self.protocol.decode(VERSION, topic, b""), {})
        buff = bytes.fromhex("08 00 10 02 18 ff ff ff ff ff ff ff ff ff 01 20 00 28 00")
        self.assertEqual(self.protocol.decode(VERSION, "Buff", buff),
                         {"robot_id": 0, "buff_type": 2, "buff_level": -1,
                          "buff_max_time": 0, "buff_left_time": 0})
        self.assertEqual(self.protocol.decode(VERSION, "RobotPerformanceSelectionSync",
                                             bytes.fromhex("08 00 10 ff ff ff ff 0f 18 00")),
                         {"shooter": 0, "chassis": 4294967295, "sentry_control": 0})
        self.assertEqual(self.protocol.decode(VERSION, "DeployModeStatusSync", b"\x08\x00"), {"status": 0})
        self.assertEqual(self.protocol.decode(VERSION, "Buff", b"\x10\x63\x30\x01"), {"buff_type": 99})
        for topic in schemas:
            with self.assertRaises(ValueError):
                self.protocol.decode(VERSION, topic, b"\x08")

    def test_wire_boundary_unknowns_and_version(self):
        self.assertEqual(self.protocol.decode(VERSION, "GameStatus", b"\x28\x04\xf8\x07\x01"),
                         {"current_stage": 4})  # Unknown field 127 remains forward-compatible.
        for payload in (b"\x28", b"\x80", b"\x2a\x05\x01", b"\x00", b"x" * 65537):
            with self.assertRaises(ValueError):
                self.protocol.decode(VERSION, "GameStatus", payload)
        for topic in ("rm27/sim/v1/referee", "CommonCommand", "RadarInfoToClient", "GameStatus "):
            with self.assertRaises(ValueError):
                self.protocol.decode(VERSION, topic, b"\x08\x01")
        with self.assertRaises(ValueError):
            self.protocol.decode("rm2027", "GameStatus", b"\x28\x04")
        with self.assertRaises(ValueError):
            self.protocol.decode(VERSION, "RobotPosition", b"\x0d" + struct.pack("<f", float("nan")))
        with self.assertRaises(ValueError):
            self.protocol.decode(VERSION, "CustomByteBlock", b"\x0a\xad\x02" + b"x" * 301)

    def test_independent_freshness_and_disconnect(self):
        clock = [0]
        snapshot = Snapshot(self.protocol, lambda: clock[0])
        self.assertFalse(snapshot.accept(VERSION, "GameStatus", b""))
        self.assertFalse(snapshot.accept(VERSION, "GameStatus", b"\x28\x04", retained=True))
        self.assertIsNone(snapshot.fresh("GameStatus"))
        self.assertTrue(snapshot.accept(VERSION, "GameStatus", b"\x28\x04"))
        clock[0] = 1000000000
        snapshot.accept(VERSION, "RobotDynamicStatus", b"\x08\x00")
        clock[0] = 1500000000
        self.assertIsNone(snapshot.fresh("GameStatus"))
        self.assertEqual(snapshot.fresh("RobotDynamicStatus"), {"current_health": 0})
        with self.assertRaises(ValueError):
            snapshot.accept(VERSION, "RobotDynamicStatus", b"\x08")
        clock[0] = 2500000000
        self.assertIsNone(snapshot.fresh("RobotDynamicStatus"))  # Invalid did not refresh it.
        snapshot.accept(VERSION, "RobotDynamicStatus", b"\x08\x01")
        snapshot.reset()
        self.assertIsNone(snapshot.fresh("RobotDynamicStatus"))
        snapshot.accept(VERSION, "GlobalUnitStatus", b"\x5a\x0a" + bytes(range(10)))
        snapshot.fresh("GlobalUnitStatus")["robot_health"][0] = 999
        self.assertEqual(snapshot.fresh("GlobalUnitStatus")["robot_health"][0], 0)

    def test_offline_command_schema_only(self):
        keys = self.protocol.message("KeyboardMouseControl")
        keys.mouse_y = -1
        keys.keyboard_value = 0xffff
        parsed = self.protocol.message("KeyboardMouseControl")
        parsed.ParseFromString(keys.SerializeToString())
        self.assertEqual(parsed.mouse_y, -1)
        self.assertEqual(parsed.keyboard_value, 65535)
        self.assertNotIn("KeyboardMouseControl", INBOUND_TOPICS)
        with self.assertRaises(KeyError):
            self.protocol.message("RadarInfoToClient")

    def test_cli_binary_sample_and_missing_file(self):
        with TemporaryDirectory() as directory:
            payload = Path(directory) / "sample.bin"
            payload.write_bytes(b"\x28\x04\x30\xa4\x03")
            command = [sys.executable, str(Path(__file__).resolve().parents[1] / "tools" / "rm2026_protocol.py"),
                       "--descriptor", str(DESCRIPTOR.resolve()), "--protocol", VERSION,
                       "--topic", "GameStatus", "--payload", str(payload)]
            result = subprocess.run(command, capture_output=True, text=True, cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["fields"],
                             {"current_stage": 4, "stage_countdown_sec": 420})
            command[-1] = str(Path(directory) / "missing.bin")
            result = subprocess.run(command, capture_output=True, text=True, cwd=directory)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--descriptor", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    DESCRIPTOR = args.descriptor
    unittest.main(argv=[sys.argv[0]] + remaining)
