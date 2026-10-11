"""Canonical state, fault/replay boundaries and actual loopback session checks."""

import json
import socket
import tempfile
import time
import unittest
from pathlib import Path

from simulator.console_session import (
    ConsoleSession, Recorder, VideoFaultQueue, default_scene, default_settings,
    scenario_steps, validate_scene,
)
from simulator.rm27_simulator import load_replay
from simulator.sim_mqtt_broker import TOPIC, encode_packet, receive_packet
from simulator.sim_state import default_state, state_packet, validate_event, validate_state


def wait_until(predicate, timeout=3):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError("condition timed out")


def drain_until_idle(receiver, timeout=0.5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        receiver.settimeout(min(.15, max(.001, deadline - time.monotonic())))
        try:
            receiver.recv(4096)
        except socket.timeout:
            if time.monotonic() < deadline:
                return
            break
    raise AssertionError("接收端持续收到意外数据，已到达排空总超时")


def observer(port):
    client = socket.create_connection(("127.0.0.1", port), timeout=2)
    client.settimeout(2)
    client.sendall(encode_packet(0x10, b"\x00\x04MQTT\x04\x02\x00\x00\x00\x04test"))
    assert receive_packet(client) == (0x20, b"\x00\x00")
    client.sendall(encode_packet(0x82, b"\x00\x01" + len(TOPIC).to_bytes(2, "big") + TOPIC + b"\x00"))
    assert receive_packet(client) == (0x90, b"\x00\x01\x00")
    return client


class ConsoleStateTests(unittest.TestCase):
    def test_team_identity_and_all_self_fields_share_one_robot(self):
        state = default_state()
        for team in ("red", "blue"):
            state["team"] = team
            for index in range(6):
                state["self_index"] = index
                robot = state["robots"][team][index]
                robot.update(hp=0, shield=12, level=4, ammo=123)
                payload = json.loads(state_packet(validate_state(state), 0, 1234))
                self.assertEqual(payload["telemetry"]["hp"], payload["hud"]["friendly_hp"][index])
                self.assertEqual(payload["telemetry"]["hp"], 0)
                self.assertEqual(payload["hud"]["own_team"], team)
                self.assertEqual((payload["hud"]["shield"], payload["hud"]["level"],
                                  payload["telemetry"]["ammo"]), (12, 4, 123))
                self.assertLessEqual(len(state_packet(state, 0, 1234)), 4096)

    def test_invalid_state_and_scenes(self):
        for field, value in (("remaining_seconds", -1), ("self_index", True),
                             ("pitch_deg", float("nan")), ("team", "green")):
            state = default_state()
            state[field] = value
            with self.assertRaises(ValueError):
                validate_state(state)
        state = default_state()
        state["robots"]["red"][0]["hp"] = 1001
        with self.assertRaises(ValueError):
            validate_state(state)
        for key, value in (("delay_ms", 1001), ("loss_pct", -1),
                           ("video_path", "https://example.com/video.ts"),
                           ("video_path", r"\\server\video.ts"), ("transport", "official")):
            scene = default_scene()
            scene["settings"][key] = value
            with self.assertRaises(ValueError):
                validate_scene(scene)
        scene = default_scene()
        scene["steps"] = [dict(at_ms=10, changes={}), dict(at_ms=1, changes={})]
        with self.assertRaises(ValueError):
            validate_scene(scene)

    def test_event_bounds_and_phase_script(self):
        event = dict(id="sim-test-1", category="robot", text="模拟：机器人状态更新")
        self.assertEqual(validate_event(event), event)
        for value in ("hello\nworld", "😀" * 61):
            with self.assertRaises(ValueError):
                validate_event(dict(event, text=value))
        self.assertEqual(scenario_steps("独立断流恢复")[2],
                         dict(at_ms=7000, changes=dict(video_enabled=False)))
        self.assertEqual(validate_scene(default_scene()), default_scene())


class FaultAndReplayTests(unittest.TestCase):
    def test_seeded_faults_are_bounded_and_repeatable(self):
        cfg = dict(default_settings(), loss_pct=20.0, delay_ms=30, jitter_ms=40,
                   reorder_pct=50.0, corrupt_pct=20.0)
        a, b = VideoFaultQueue(cfg), VideoFaultQueue(cfg)
        for i in range(100):
            packet = b"\x47" + bytes([i]) * 187
            a.enqueue(i * .02, packet)
            b.enqueue(i * .02, packet)
        self.assertEqual(a.pending, b.pending)
        self.assertEqual(list(a.due(10)), list(b.due(10)))
        self.assertGreater(a.dropped, 0)
        self.assertGreater(a.corrupted, 0)
        full = VideoFaultQueue(dict(default_settings(), delay_ms=1000))
        for _ in range(1000):
            full.enqueue(0, b"x")
        self.assertEqual(len(full.pending), 256)
        self.assertEqual(full.dropped, 744)
        full.clear()
        self.assertEqual(list(full.due(10)), [])

    def test_v2_records_impaired_ts_and_rejects_invalid_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.rm27-replay"
            packet = b"\x46" + bytes(187) + (b"\x47" + bytes(187)) * 6
            recorder = Recorder(path)
            recorder.write(5, "video_ts_raw", packet)
            recorder.write(4, "referee", state_packet(default_state(), 1, 1234))
            recorder.close()
            self.assertEqual(load_replay(path)[0], (5, "video_ts_raw", packet))
            self.assertEqual(load_replay(path)[1][0], 5)
            with self.assertRaises(FileExistsError):
                Recorder(path)
            for channel, payload in (("video_ts_raw", b"x" * 188 * 7),
                                      ("control", b"{}"), ("referee", b"[]")):
                other = Path(directory) / (channel + ".rm27-replay")
                recorder = Recorder(other)
                recorder.write(0, channel, payload)
                recorder.close()
                with self.assertRaises(ValueError):
                    load_replay(other)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.receivers = []
        for _ in range(3):
            receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            receiver.bind(("127.0.0.1", 0))
            receiver.settimeout(1)
            self.receivers.append(receiver)
            self.addCleanup(receiver.close)
        self.referee, self.probe, self.ts = self.receivers
        self.session = ConsoleSession(referee_port=self.referee.getsockname()[1],
                                      video_port=self.probe.getsockname()[1],
                                      ts_port=self.ts.getsockname()[1], mqtt_port=0)

    def tearDown(self):
        self.session.stop()
        if self.session.worker:
            self.session.worker.join(4)
            self.assertFalse(self.session.running)

    def test_drain_has_total_timeout_with_continuous_sender(self):
        self.session.start(default_scene())
        self.referee.recv(4096)
        started = time.monotonic()
        with self.assertRaisesRegex(AssertionError, "总超时"):
            drain_until_idle(self.referee, timeout=.25)
        self.assertLess(time.monotonic() - started, 1)

    def test_udp_manual_updates_stale_recovery_and_recording(self):
        with tempfile.TemporaryDirectory() as directory:
            receiver = self.referee
            receiver.settimeout(1)
            scene = default_scene()
            record = Path(directory) / "session.rm27-replay"
            self.session.start(scene, record=record)
            first = json.loads(receiver.recv(4096))
            scene["state"]["robots"]["blue"][5]["hp"] = 0
            self.session.post("scene", scene)
            for _ in range(5):
                updated = json.loads(receiver.recv(4096))
                if updated["telemetry"]["hp"] == 0:
                    break
            self.assertEqual(updated["hud"]["friendly_hp"][5], 0)
            self.assertEqual(updated["telemetry"]["hp"], 0)
            self.session.post("switch", dict(referee_enabled=False))
            wait_until(lambda: not self.session.snapshot["settings"]["referee_enabled"])
            receiver.settimeout(.15)
            drain_until_idle(receiver)
            with self.assertRaises(socket.timeout):
                receiver.recv(4096)
            self.session.post("switch", dict(referee_enabled=True))
            receiver.settimeout(1)
            recovered = json.loads(receiver.recv(4096))
            self.assertGreater(recovered["sequence"], first["sequence"])
            for category in ("system", "robot"):
                self.session.post("event", dict(id="sim-" + category, category=category,
                                                text="模拟：" + category))
            categories = set()
            for _ in range(15):
                packet = json.loads(receiver.recv(4096))
                if "event" in packet:
                    categories.add(packet["event"]["category"])
                if len(categories) == 2:
                    break
            self.assertEqual(categories, {"system", "robot"})
            self.session.stop()
            self.session.worker.join(3)
            events = load_replay(record)
            self.assertEqual(events[0][1], "control")
            self.assertEqual(events[-1][1], "control")
            self.assertIn("场景结束", json.loads(events[-1][2])["text"])

    def test_actual_ts_outage_and_restart_discards_delayed_queue(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)  # tearDown closes the sender before removing its file.
        with self.ts as receiver:
            sample = Path(directory.name) / "sample.ts"
            packet = (b"\x47" + bytes(187)) * 7
            sample.write_bytes(packet * 3)
            receiver.settimeout(1)
            scene = default_scene()
            scene["settings"].update(video_mode="ts", video_path=str(sample), delay_ms=30)
            self.session.start(scene)
            self.assertEqual(receiver.recv(2048), packet)
            self.session.post("switch", dict(video_enabled=False))
            wait_until(lambda: not self.session.snapshot["settings"]["video_enabled"])
            self.assertEqual(self.session.snapshot["queued"], 0)
            receiver.settimeout(.15)
            drain_until_idle(receiver)
            self.assertGreater(self.session.snapshot["sent"]["referee"], 0)
            self.session.post("switch", dict(video_enabled=True))
            receiver.settimeout(1)
            self.assertEqual(receiver.recv(2048), packet)

    def test_mqtt_uses_same_state_reconnects_without_old_events(self):
        scene = default_scene()
        scene["settings"]["transport"] = "mqtt"
        self.session.start(scene)
        wait_until(lambda: "settings" in self.session.snapshot)
        with observer(self.session.mqtt_port) as client:
            _, body = receive_packet(client)
            payload = json.loads(body[2 + len(TOPIC):])
            self.assertEqual(payload["hud"]["friendly_hp"][5], payload["telemetry"]["hp"])
            sequence = payload["sequence"]
            self.session.post("switch", dict(mqtt_enabled=False))
            wait_until(lambda: not self.session.snapshot["settings"]["mqtt_enabled"])
        self.session.post("switch", dict(mqtt_enabled=True))
        wait_until(lambda: self.session.snapshot["settings"]["mqtt_enabled"])
        with observer(self.session.mqtt_port) as client:
            _, body = receive_packet(client)
            payload = json.loads(body[2 + len(TOPIC):])
            self.assertGreater(payload["sequence"], sequence)
            self.assertNotIn("event", payload)

    def test_script_and_recorded_ts_replay_match_wire_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            receiver = self.ts
            path = Path(directory) / "wire.rm27-replay"
            packet = (b"\x47" + bytes(187)) * 7
            recorder = Recorder(path)
            recorder.write(0, "video_ts_raw", packet)
            recorder.write(120, "video_ts_raw", packet)
            recorder.close()
            receiver.settimeout(2)
            self.session.start(default_scene(), replay=path)
            self.assertEqual(receiver.recv(2048), packet)
            before = time.monotonic()
            self.assertEqual(receiver.recv(2048), packet)
            self.assertGreater(time.monotonic() - before, .06)
            self.session.worker.join(3)
            self.assertFalse(self.session.running)


if __name__ == "__main__":
    unittest.main()
