"""Checks for the loopback-only MPEG-TS test sender."""

import socket
import tempfile
import unittest
from pathlib import Path

from simulator.stream_video import stream_video, validate_sample


class StreamVideoTest(unittest.TestCase):
    def test_rejects_non_ts_data(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.ts"
            path.write_bytes(b"x" * 188)
            with self.assertRaises(ValueError):
                validate_sample(path)

    def test_sends_only_to_loopback(self) -> None:
        packet = bytes([0x47]) + bytes(187)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.ts"
            path.write_bytes(packet * 7)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
                receiver.bind(("127.0.0.1", 0))
                receiver.settimeout(1)
                stream_video(path, 20000, False, port=receiver.getsockname()[1])
                payload, address = receiver.recvfrom(2048)
                self.assertEqual(payload, packet * 7)
                self.assertEqual(address[0], "127.0.0.1")


if __name__ == "__main__":
    unittest.main()
