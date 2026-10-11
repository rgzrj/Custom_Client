import json
import unittest

from simulator.sim_radar import build_radar_packet


class RadarTest(unittest.TestCase):
    def test_generated_full_snapshots(self):
        for sequence in (0, 1, 299, 9007199254740991):
            payload = build_radar_packet(sequence, 1790000000000)
            self.assertLess(len(payload), 4096)
            packet = json.loads(payload)
            self.assertEqual(packet["channel"], "rm27.sim.v1.radar")
            for team in ("red", "blue"):
                self.assertEqual([p["id"] for p in packet[team]], list(range(6)))
                for point in packet[team]:
                    self.assertTrue(0 <= point["x"] <= 1 and 0 <= point["y"] <= 1)
        for sequence, timestamp in ((True, 1), (-1, 1), (1, 0), (1, 1.5)):
            with self.assertRaises(ValueError):
                build_radar_packet(sequence, timestamp)
