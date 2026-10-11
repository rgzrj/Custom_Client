"""Hostile loopback peer checks; no real referee or control publication."""

import argparse
from pathlib import Path
import socket
import struct
import sys
import time
import unittest

import test_rm2026_native as native
from mqtt_wire import packet, publish, read_packet

MALFORMED = [b"\x20\x00", b"\x90\x00", b"\x36\x00", b"\x00\x00",
             b"\x30\x80\x80\x80\x80",  # More than four length bytes.
             packet(0x30, b"\x00\x41" + b"x" * 65),
             packet(0x30, b"\x00\x01x" + b"x" * 65537),
             packet(0x30, b"\x00\x03a\x00b"),
             packet(0x32, b"\x00\x01x\x00\x00")]


class IngressChecks(native.NativeChecks):
    def seed(self, stream):
        self.send_mapped(stream, "RobotDynamicStatus", b"\x08\x01")

    def assert_closed_and_cleared(self, stream):
        report = self.wait(lambda state: state.get("link") == "offline")
        self.assertEqual(report["state"], {})
        self.assertEqual(report["fresh"], {})
        # Some earlier PUBACKs may still be in TCP, but the connection must close.
        stream.settimeout(2)
        while True:
            try:
                if not stream.recv(4096):
                    break
            except ConnectionResetError:
                break

    def test_oversized_header_without_body_and_reconnect(self):
        stream = self.connect_peer()
        self.seed(stream)
        # Advertise 1 MiB with no body: rejection must not wait for the body.
        stream.sendall(b"\x30\x80\x80\x40")
        self.assert_closed_and_cleared(stream)
        stream = self.connect_peer()
        report = self.wait(lambda state: state.get("sessions") == 2)
        self.assertEqual(report["state"], {})
        self.seed(stream)

    def test_malformed_headers_and_topics(self):
        wire = getattr(self, "wire", MALFORMED[0])
        with self.subTest(wire=wire[:8]):
            stream = self.connect_peer()
            self.seed(stream)
            stream.sendall(wire)
            self.assert_closed_and_cleared(stream)

    def test_packet_flood_clears_and_recovers(self):
        stream = self.connect_peer()
        self.seed(stream)
        wire = packet(0x30, b"\x00\x12RobotDynamicStatus\x08\x01")
        try:
            stream.sendall(wire * 2000)
        except (ConnectionResetError, BrokenPipeError):
            pass
        self.assert_closed_and_cleared(stream)
        stream = self.connect_peer()
        self.seed(stream)

    def test_byte_flood_and_maximum_valid_payload(self):
        stream = self.connect_peer()
        self.seed(stream)
        # Largest allowed topic and payload reach the callback, then whitelist rejects.
        rejected = self.report["counts"]["rejected"]
        publish(stream, "x" * 64, b"x" * 65536, mid=12)
        self.assertEqual(read_packet(stream), (0x40, struct.pack("!H", 12)))
        self.wait(lambda state: state.get("counts", {}).get("rejected") == rejected + 1)
        time.sleep(1.05)  # Independent byte budget, not packet-count overflow.
        wire = packet(0x30, b"\x00\x01x" + b"x" * 65536)
        try:
            stream.sendall(wire * 32)
        except (ConnectionResetError, BrokenPipeError):
            pass
        self.assert_closed_and_cleared(stream)

    def test_truncated_packet_disconnect(self):
        stream = self.connect_peer()
        self.seed(stream)
        stream.sendall(b"\x30\x40\x00\x12RobotDynamicStatus")
        stream.shutdown(socket.SHUT_RDWR)
        self.wait(lambda state: state.get("link") == "offline")
        self.assertEqual(self.report["state"], {})

    def test_event_history_bounded_and_session_reset(self):
        stream = self.connect_peer()
        self.seed(stream)
        for index in range(40):
            publish(stream, "Event", b"\x08\x09", mid=index + 1)
            self.assertEqual(read_packet(stream), (0x40, struct.pack("!H", index + 1)))
        report = self.wait(lambda state: state.get("event_history_evicted") == "16")
        self.assertEqual(report["event_history_size"], 24)
        # Same event type is not a unique event ID: each non-DUP arrival counts.
        publish(stream, "Event", b"\x08\x09", mid=40, duplicate=True)
        self.assertEqual(read_packet(stream), (0x40, struct.pack("!H", 40)))
        report = self.wait(lambda state: state.get("counts", {}).get("duplicates") == 1)
        self.assertEqual(report["event_history_evicted"], "16")
        report = self.wait(lambda state: "Event" in state.get("stale", []))
        self.assertEqual(report["event_history_size"], 24)  # History, not fresh-state evidence.
        self.send_mapped(stream, "RobotStaticStatus", b"\x20\x65")
        report = self.wait(lambda state: state.get("event_history_size") == 0)
        self.assertEqual(report["event_history_evicted"], "0")
        self.send_mapped(stream, "Event", b"\x08\x01")
        self.wait(lambda state: state.get("event_history_size") == 1)
        stream.shutdown(socket.SHUT_RDWR)
        report = self.wait(lambda state: state.get("link") == "offline")
        self.assertEqual(report["event_history_size"], 0)
        stream = self.connect_peer()
        self.assertEqual(self.report["event_history_size"], 0)


def load_tests(loader, tests, pattern):
    # Reuse the wire harness, not its already separately registered base suite.
    suite = unittest.TestSuite(IngressChecks(name) for name in IngressChecks.__dict__
                              if name.startswith("test_") and name != "test_malformed_headers_and_topics")
    for wire in MALFORMED:
        case = IngressChecks("test_malformed_headers_and_topics")
        case.wire = wire  # Each variant owns a fresh process and port, no retry-time accumulation.
        suite.addTest(case)
    return suite


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    native.EXE = args.exe.resolve()
    unittest.main(argv=[sys.argv[0]] + remaining)
