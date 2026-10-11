"""Stream a local MPEG-TS H.264/H.265 sample to the loopback video test port.

This is a project-only test transport, not RoboMaster UDP framing.
"""

import argparse
import socket
import time
from pathlib import Path

PORT = 39003
PACKET_BYTES = 188 * 7
MAX_FILE_BYTES = 64 * 1024 * 1024


def validate_sample(path: Path) -> None:
    if path.suffix.lower() != ".ts" or not path.is_file():
        raise ValueError("需要已有的 .ts 视频文件")
    size = path.stat().st_size
    if not 0 < size <= MAX_FILE_BYTES or size % 188:
        raise ValueError("TS 文件大小无效或超过 64 MiB")
    with path.open("rb") as stream:
        while chunk := stream.read(PACKET_BYTES):
            if len(chunk) % 188 or any(chunk[i] != 0x47 for i in range(0, len(chunk), 188)):
                raise ValueError("文件不是完整的 MPEG-TS 包序列")


def stream_video(path: Path, bitrate_kbps: int, loop: bool, *, port=PORT) -> None:
    if type(port) is not int or not 1 <= port <= 65535:
        raise ValueError("测试端口应为 1–65535 整数")
    if not 64 <= bitrate_kbps <= 20000:
        raise ValueError("码率应在 64–20000 kbps")
    validate_sample(path)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        while True:
            with path.open("rb") as stream:
                target = time.monotonic()
                while chunk := stream.read(PACKET_BYTES):
                    sender.sendto(chunk, ("127.0.0.1", port))
                    target += len(chunk) * 8 / (bitrate_kbps * 1000)
                    time.sleep(max(0, target - time.monotonic()))
            if not loop:
                return


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sample", type=Path, help="H.264/H.265 MPEG-TS 测试文件")
    parser.add_argument("--bitrate-kbps", type=int, default=500)
    parser.add_argument("--loop", action="store_true")
    args = parser.parse_args()
    stream_video(args.sample, args.bitrate_kbps, args.loop)
