import json
import tempfile
import unittest
from pathlib import Path

from simulator.rm27_simulator import (
    CHANNEL,
    REPLAY_FORMAT,
    VIDEO_PREFIX,
    build_referee_packet,
    build_video_datagram,
    event_for_sequence,
    links_for_scenario,
    load_replay,
    phase_for_sequence,
    write_replay_event,
    write_replay_header,
)


class SimulatorPacketTests(unittest.TestCase):
    def test_referee_packet_is_deterministic_and_namespaced(self) -> None:
        first = build_referee_packet(21, 1_789_999_999_000)
        second = build_referee_packet(21, 1_789_999_999_000)
        self.assertEqual(first, second)

        payload = json.loads(first)
        self.assertEqual(payload["channel"], CHANNEL)
        self.assertEqual(payload["phase"], "match")
        self.assertEqual(payload["sequence"], 21)
        self.assertEqual(payload["hud"]["own_team"], "blue")
        self.assertEqual(len(payload["hud"]["friendly_hp"]), 6)
        self.assertEqual(len(payload["hud"]["friendly_positions"]), 6)
        self.assertTrue(all(0 <= axis <= 1 for point in payload["hud"]["enemy_positions"] for axis in point))
        self.assertEqual(json.loads(build_referee_packet(200, 1_789_999_999_000))["hud"]["own_team"], "blue")
        self.assertEqual(json.loads(build_referee_packet(200, 1_789_999_999_000, "red"))["hud"]["own_team"], "red")

    def test_phase_sequence(self) -> None:
        self.assertEqual(phase_for_sequence(0), "preparation")
        self.assertEqual(phase_for_sequence(10), "countdown")
        self.assertEqual(phase_for_sequence(20), "match")

    def test_simulation_events_are_repeated_with_stable_ids(self) -> None:
        self.assertIsNone(event_for_sequence(29))
        self.assertEqual(event_for_sequence(30), event_for_sequence(34))
        self.assertIsNone(event_for_sequence(35))
        self.assertEqual(event_for_sequence(80), event_for_sequence(84))
        self.assertNotEqual(event_for_sequence(30)["category"], event_for_sequence(80)["category"])
        self.assertEqual(json.loads(build_referee_packet(30, 1_789_999_999_000))["event"],
                         event_for_sequence(30))

    def test_hud_status_script_is_deterministic(self) -> None:
        before = json.loads(build_referee_packet(29, 1_789_999_999_000))["hud"]
        upgraded = json.loads(build_referee_packet(30, 1_789_999_999_000))["hud"]
        firing = json.loads(build_referee_packet(45, 1_789_999_999_000))["hud"]
        climbing = json.loads(build_referee_packet(125, 1_789_999_999_000))["hud"]
        self.assertEqual((before["level"], upgraded["level"]), (2, 3))
        self.assertTrue(0 <= upgraded["shield"] <= upgraded["max_shield"])
        self.assertTrue(firing["actions"]["fire"])
        self.assertTrue(climbing["actions"]["climb"])

    def test_video_probe_packet_is_not_a_codec_stream(self) -> None:
        packet = build_video_datagram(3, 128)
        self.assertTrue(packet.startswith(VIDEO_PREFIX))
        self.assertEqual(len(packet), 128)

    def test_link_recovery_scenario_is_deterministic(self) -> None:
        self.assertEqual(links_for_scenario("link-recovery", 2.9), (True, True))
        self.assertEqual(links_for_scenario("link-recovery", 3.0), (False, True))
        self.assertEqual(links_for_scenario("link-recovery", 7.0), (True, False))
        self.assertEqual(links_for_scenario("link-recovery", 12.0), (True, True))

    def test_replay_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.rm27-replay"
            referee = build_referee_packet(0, 1_789_999_999_000)
            video = build_video_datagram(0, 128)
            with path.open("xb") as stream:
                write_replay_header(stream, "healthy", 10.0, 30.0)
                write_replay_event(stream, 0, "referee", referee)
                write_replay_event(stream, 0, "video", video)

            self.assertEqual(
                load_replay(path),
                [(0, "referee", referee), (0, "video", video)],
            )

    def test_replay_rejects_non_simulation_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.rm27-replay"
            with path.open("xb") as stream:
                stream.write(json.dumps({"format": REPLAY_FORMAT}).encode("ascii") + b"\n")
                write_replay_event(stream, 0, "referee", b"{}")

            with self.assertRaisesRegex(ValueError, "non-simulation"):
                load_replay(path)


if __name__ == "__main__":
    unittest.main()
