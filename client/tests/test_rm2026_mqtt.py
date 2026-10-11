"""Loopback wire checks; this tiny scripted peer is not a general MQTT broker."""

import argparse
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from rm2026_mqtt_observer import Observer
from rm2026_protocol import INBOUND_TOPICS, Protocol, VERSION
from mqtt_wire import packet, read_packet, publish


class MqttChecks(unittest.TestCase):
    def setUp(self):
        self.server = socket.socket()
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(1)
        self.server.settimeout(5)
        self.addCleanup(self.server.close)
        self.now = 0
        self.observer = Observer(Protocol(DESCRIPTOR), VERSION,
                                 port=self.server.getsockname()[1], clock=lambda: self.now)
        self.observer.start()
        self.addCleanup(self.observer.stop)

    def wait(self, predicate):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            status = self.observer.status()
            if predicate(status):
                return status
            time.sleep(0.01)
        self.fail("observer did not reach expected state: " + str(self.observer.status()))

    def connect_peer(self, grants=None, accept=True):
        stream, address = self.server.accept()
        stream.settimeout(3)
        self.addCleanup(stream.close)
        self.assertEqual(address[0], "127.0.0.1")
        header, body = read_packet(stream)
        self.assertEqual(header, 0x10)
        self.assertEqual(body[:10], b"\x00\x04MQTT\x04\x02\x00\x0a")
        name_length = struct.unpack("!H", body[10:12])[0]
        self.assertEqual(name_length, len(body[12:]))
        self.assertTrue(body[12:].startswith(b"rm2026-local-observer-"))
        self.assertNotEqual(body[12:], b"101")  # Not a robot login identity.
        if not accept:
            stream.sendall(packet(0x20, b"\x00\x05"))
            return stream
        # Deliberately split a CONNACK across TCP writes.
        stream.sendall(b"\x20")
        stream.sendall(b"\x02\x00\x00")
        header, body = read_packet(stream)
        self.assertEqual(header, 0x82)
        mid = body[:2]
        topics = []
        offset = 2
        while offset < len(body):
            size = struct.unpack("!H", body[offset:offset + 2])[0]
            offset += 2
            topics.append(body[offset:offset + size].decode("utf-8"))
            offset += size
            self.assertEqual(body[offset], 1)
            offset += 1
        self.assertEqual(topics, sorted(INBOUND_TOPICS))
        self.assertEqual(self.observer.status()["link"], "awaiting-suback")
        # Even valid data before SUBACK is not live evidence.
        publish(stream, "GameStatus", b"\x28\x04", mid=600)
        self.assertEqual(read_packet(stream), (0x40, struct.pack("!H", 600)))
        self.assertEqual(self.observer.status()["fresh"], {})
        if grants is None:
            grants = [1] * len(topics)
        stream.sendall(packet(0x90, mid + bytes(grants)))
        expected = "subscribed" if len(grants) == len(topics) and all(
            code in (0, 1) for code in grants) else "subscription-rejected"
        self.wait(lambda state: state["link"] == expected)
        return stream

    def test_receive_ack_deduplicate_freshness_and_limits(self):
        stream = self.connect_peer()
        publish(stream, "GameStatus", b"\x28\x04\x30\xa4\x03", mid=7)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.assertEqual(self.observer.status()["fresh"]["GameStatus"],
                         {"current_stage": 4, "stage_countdown_sec": 420})
        self.now = 1400000000
        publish(stream, "GameStatus", b"\x28\x04\x30\xa4\x03", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.assertEqual(self.observer.status()["counts"]["duplicates"], 1)
        self.now = 1500000000
        self.assertEqual(self.observer.status()["stale"], ["GameStatus"])
        self.assertEqual(self.observer.status()["fresh"], {})
        # A normal new delivery may legally reuse MID and identical field values.
        publish(stream, "GameStatus", b"\x28\x04\x30\xa4\x03", mid=7)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.assertIn("GameStatus", self.observer.status()["fresh"])
        self.now = 2000000000
        publish(stream, "RobotDynamicStatus", b"\x08\x00")
        self.wait(lambda state: "RobotDynamicStatus" in state["fresh"])
        self.assertEqual(self.observer.status()["fresh"]["RobotDynamicStatus"], {"current_health": 0})
        rejected_before = self.observer.status()["counts"]["rejected"]
        for mid, topic, payload in (
            (8, "GameStatus", b"\x28"), (9, "GameStatus", b""),
            (10, "GameStatus", b"\xf8\x07\x01"),
            (11, "RadarInfoToClient", b"\x08\x01"),
            (12, "CommonCommand", b"\x08\x01"),
            (13, "rm27/sim/v1/referee", b'{"schema":"sim.v1"}'),
            (14, "GameStatus", b"x" * 65537),
        ):
            publish(stream, topic, payload, mid=mid)
            self.assertEqual(read_packet(stream), (0x40, struct.pack("!H", mid)))
        stream.sendall(packet(0x32, b"\x00\x01\xff\x00\x10\x28\x04"))
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x10"))
        self.assertEqual(self.observer.status()["counts"]["rejected"], rejected_before + 8)
        self.now = 3000000000
        publish(stream, "GameStatus", b"\x28\x04", mid=15, retained=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x0f"))
        self.assertNotIn("GameStatus", self.observer.status()["fresh"])
        self.assertIn("RobotDynamicStatus", self.observer.status()["fresh"])
        self.assertEqual(self.observer.status()["counts"]["retained"], 1)
        self.now = 3500000000
        self.assertEqual(self.observer.status()["fresh"], {})
        # Cache is bounded; unknown/evicted DUPs also cannot refresh freshness.
        for mid in range(1000, 1260):
            publish(stream, "GameStatus", b"\x28\x04", mid=mid)
            self.assertEqual(read_packet(stream), (0x40, struct.pack("!H", mid)))
        self.assertEqual(len(self.observer._duplicates), 256)
        self.now = 5000000000
        publish(stream, "GameStatus", b"\x28\x04", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.assertEqual(self.observer.status()["fresh"], {})
        self.assertFalse(hasattr(self.observer, "publish"))
        self.observer.stop()
        self.assertEqual(read_packet(stream), (0xe0, b""))  # No outbound PUBLISH/control.
        self.assertEqual(self.observer.status()["fresh"], {})

    def test_disconnect_reconnect_resubscribe_clears_old_data(self):
        stream = self.connect_peer()
        publish(stream, "GameStatus", b"\x28\x04", mid=7)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        stream.shutdown(socket.SHUT_RDWR)
        stream.close()
        self.wait(lambda state: state["link"] == "offline")
        self.assertEqual(self.observer.status()["fresh"], {})
        stream = self.connect_peer(grants=[0] * len(INBOUND_TOPICS))
        self.assertEqual(self.observer.status()["sessions"], 2)
        self.assertEqual(self.observer.status()["fresh"], {})
        publish(stream, "GameStatus", b"\x28\x04", mid=7, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.assertEqual(self.observer.status()["fresh"], {})
        publish(stream, "GameStatus", b"\x28\x04", mid=7)
        self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
        self.assertIn("GameStatus", self.observer.status()["fresh"])

    def test_denied_and_partial_suback_do_not_become_live(self):
        for grants in ([1] * (len(INBOUND_TOPICS) - 1) + [0x80], [1] * (len(INBOUND_TOPICS) - 1),
                       [1] * (len(INBOUND_TOPICS) - 1) + [2]):
            stream = self.connect_peer(grants=grants)
            publish(stream, "GameStatus", b"\x28\x04", mid=7)
            self.assertEqual(read_packet(stream), (0x40, b"\x00\x07"))
            self.assertEqual(self.observer.status()["fresh"], {})
            stream.shutdown(socket.SHUT_RDWR)
            stream.close()
            self.wait(lambda state: state["link"] == "offline")

    def test_connack_refusal_and_retry(self):
        stream = self.connect_peer(accept=False)
        self.wait(lambda state: state["link"] in ("connection-rejected", "offline"))
        self.assertEqual(self.observer.status()["fresh"], {})
        stream.close()
        self.connect_peer()
        self.assertEqual(self.observer.status()["sessions"], 1)

    def test_configuration_and_cli_reject_external_endpoints(self):
        for host in ("localhost", "0.0.0.0", "::1", "192.168.12.1", "example.com"):
            with self.assertRaises(ValueError):
                Observer(Protocol(DESCRIPTOR), VERSION, host=host)
        for port in (0, 65536):
            with self.assertRaises(ValueError):
                Observer(Protocol(DESCRIPTOR), VERSION, port=port)
        with self.assertRaises(ValueError):
            Observer(Protocol(DESCRIPTOR), "rm2027")
        with self.assertRaises(ValueError):
            self.observer.status(0)
        command = [sys.executable, str(Path(__file__).resolve().parents[1] / "tools" / "rm2026_mqtt_observer.py"),
                   "--descriptor", str(DESCRIPTOR.resolve()), "--protocol", VERSION,
                   "--host", "192.168.12.1"]
        result = subprocess.run(command, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 2)
        self.assertIn("127.0.0.1", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--descriptor", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    DESCRIPTOR = args.descriptor
    unittest.main(argv=[sys.argv[0]] + remaining)
