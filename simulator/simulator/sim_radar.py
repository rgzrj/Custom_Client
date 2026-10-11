"""Independent localhost-only mock radar; NOT an official RM27 protocol."""

import argparse
import json
import math
import socket
import time


def build_radar_packet(sequence: int, sent_at_unix_ms: int) -> bytes:
    if (type(sequence) is not int or not 0 <= sequence <= 9007199254740991
            or type(sent_at_unix_ms) is not int or not 1 <= sent_at_unix_ms <= 9007199254740991):
        raise ValueError("sequence/timestamp out of simulation bounds")
    packet = {"channel": "rm27.sim.v1.radar", "sequence": sequence,
              "sent_at_unix_ms": sent_at_unix_ms}
    for team, offset in (("red", 0), ("blue", math.pi)):
        packet[team] = [
            {"id": index,
             "x": round(0.5 + 0.38 * math.sin(sequence / 20 + index * 0.5 + offset), 4),
             "y": round(0.5 + 0.38 * math.cos(sequence / 30 + index * 0.5 + offset), 4)}
            for index in range(6)]
    return json.dumps(packet, separators=(",", ":"), allow_nan=False).encode("utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=39001, help="client local mock data port")
    parser.add_argument("--duration", type=float, default=30, help="seconds; 0 runs until Ctrl+C")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not math.isfinite(args.duration) or args.duration < 0:
        parser.error("port must be 1..65535; duration must be finite and nonnegative")
    print(f"模拟雷达 -> 127.0.0.1:{args.port}，10 Hz；Ctrl+C 停止（非官方协议）")
    started = time.monotonic()
    sequence = 0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        try:
            while args.duration == 0 or time.monotonic() - started < args.duration:
                sender.sendto(build_radar_packet(sequence, time.time_ns() // 1_000_000),
                              ("127.0.0.1", args.port))
                sequence += 1
                time.sleep(0.1)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
