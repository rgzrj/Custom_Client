"""与 C++ 接收端使用同一组独立本地线协议样本。"""

import json
import unittest
from pathlib import Path

from simulator.sim_contract import validate_packet
from simulator.sim_state import default_state, state_packet, validate_state


class ContractTests(unittest.TestCase):
    def test_structures_optional_validated_and_team_relative(self):
        state = default_state()
        state["structures"]["red"]["base"]["hp"] = 0
        state["structures"]["blue"]["outpost"]["hp"] = 321
        for team in ("red", "blue"):
            state["team"] = team
            payload = state_packet(validate_state(state), 7, 1234)
            buildings = validate_packet(payload)["hud"]["structures"]
            self.assertEqual(buildings["friendly_base"]["hp"], 0 if team == "red" else 5000)
            self.assertEqual(buildings["friendly_outpost"]["hp"], 321 if team == "blue" else 1500)
            for bad in (True, -1, 1.5, 5001):
                packet = json.loads(payload)
                packet["hud"]["structures"]["enemy_base"]["hp"] = bad
                with self.assertRaises(ValueError):
                    validate_packet(json.dumps(packet).encode())
        legacy = default_state()
        legacy.pop("structures")
        self.assertNotIn("structures", validate_state(legacy))
        self.assertNotIn("structures", validate_packet(state_packet(legacy, 0, 1234))["hud"])
        state["structures"]["red"]["base"]["hp"] = 5001
        with self.assertRaises(ValueError):
            validate_state(state)

    def test_shared_fixtures_and_actual_producer(self):
        document = json.loads((Path(__file__).resolve().parent.parent /
                               "contracts/sim-v1-fixtures.json").read_text(encoding="utf-8"))
        self.assertEqual(document["format"], "rm27.sim.contract.v1")
        self.assertEqual(len(document["cases"]), 18)
        for case in document["cases"]:
            with self.subTest(case=case["id"]):
                payload = json.dumps(case["packet"], ensure_ascii=True).encode("ascii")
                if case["accepted"]:
                    self.assertEqual(validate_packet(payload), case["packet"])
                    if "generator" in case:
                        generator = case["generator"]
                        actual = state_packet(validate_state(generator["state"]), 7,
                                              1789999999000, generator["event"])
                        self.assertEqual(json.loads(actual), case["packet"])
                else:
                    with self.assertRaises(ValueError):
                        validate_packet(payload)


if __name__ == "__main__":
    unittest.main()
