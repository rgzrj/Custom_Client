"""Deterministic localhost-only data source for the RM27 terminal.

This is a project simulation protocol. It is not an official RoboMaster
message schema and never contacts field endpoints.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import socket
import time
from pathlib import Path
from typing import BinaryIO

REFEREE_PORT = 39001
VIDEO_PORT = 39002
CHANNEL = "rm27.sim.v1.referee"
VIDEO_PREFIX = b"RM27_SIM_VIDEO_V1\0"
REPLAY_FORMAT = "rm27.sim.replay.v1"
SCENARIOS = ("healthy", "link-recovery", "referee-stale")
MAX_REPLAY_BYTES = 64 * 1024 * 1024
MAX_REPLAY_EVENTS = 200_000
MAX_REPLAY_DURATION_MS = 24 * 60 * 60 * 1000


def phase_for_sequence(sequence: int) -> str:
    if sequence < 10:
        return "preparation"
    if sequence < 20:
        return "countdown"
    return "match"


def links_for_scenario(scenario: str, elapsed_seconds: float) -> tuple[bool, bool]:
    if scenario == "healthy":
        return True, True
    if scenario == "referee-stale":
        return elapsed_seconds < 3.0, True
    if scenario == "link-recovery":
        cycle = elapsed_seconds % 12.0
        return not (3.0 <= cycle < 5.0), not (7.0 <= cycle < 9.0)
    raise ValueError(f"unknown scenario: {scenario}")


def event_for_sequence(sequence: int) -> dict[str, str] | None:
    # Repeat each event briefly so a lost UDP packet does not hide it; the UI deduplicates by id.
    if 30 <= sequence < 35:
        return {"id": "sim-upgrade-30", "category": "robot", "text": "模拟：英雄机器人升至 3 级"}
    if 80 <= sequence < 85:
        return {"id": "sim-dart-hit-80", "category": "system", "text": "模拟：飞镖命中目标"}
    return None


def build_referee_packet(sequence: int, sent_at_unix_ms: int, own_team: str = "blue") -> bytes:
    telemetry = {
        "hp": max(0, 1000 - (sequence // 80) * 20),
        "max_hp": 1000,
        "chassis_power_w": round(72.0 + 18.0 * math.sin(sequence / 8.0), 1),
        "buffer_energy_j": round(48.0 + 8.0 * math.sin(sequence / 13.0), 1),
        "barrel_heat": 28 + sequence % 30,
        "ammo": max(0, 200 - sequence // 5),
        "latency_ms": 11 + sequence % 5,
        "packet_loss_pct": round((sequence % 7) * 0.1, 1),
    }
    if own_team not in {"red", "blue"}:
        raise ValueError("own_team must be red or blue")
    hud = {
        "own_team": own_team,
        "friendly_hp": [max(0, 900 - index * 70 - sequence // 10) for index in range(6)],
        "enemy_hp": [max(0, 880 - index * 60 - sequence // 8) for index in range(6)],
        "friendly_positions": [
            [round(0.12 + index * 0.08 + 0.02 * math.sin(sequence / 15 + index), 3),
             round(0.17 + index * 0.11, 3)]
            for index in range(6)
        ],
        "enemy_positions": [
            [round(0.58 + index * 0.06, 3),
             round(0.16 + index * 0.11 + 0.02 * math.cos(sequence / 18 + index), 3)]
            for index in range(6)
        ],
        "friendly_coins": 100 + sequence // 20,
        "enemy_coins": 80 + sequence // 25,
        "restart_count": 2,
        "pitch_deg": round(20.0 * math.sin(sequence / 14.0), 1),
        # 偏航角为项目模拟可选字段（-180..180），供 HUD 的横向 yaw 刻度条联调。
        "yaw_deg": round(120.0 * math.sin(sequence / 26.0), 1),
        # 本车下标为项目模拟可选字段：每 150 包（10 Hz 下约 15 秒）换一台车，
        # 用来演示「本车身份随裁判数据变化」时队列表与左下角同步换图。
        "self_index": (sequence // 150) % 6,
        "speed_mps": round(24.0 + 3.0 * math.sin(sequence / 17.0), 1),
        "allowed_shots": max(0, 80 - sequence // 12),
        "level": 2 if sequence < 30 else 3,
        "shield": max(0, 150 - (sequence % 100)),
        "max_shield": 150,
        "actions": {
            "climb": 120 <= sequence % 180 < 130,
            "fire": 40 <= sequence % 60 < 50,
            "speed_up": 15 <= sequence % 80 < 35,
        },
    }
    payload = {
        "channel": CHANNEL,
        "sequence": sequence,
        "sent_at_unix_ms": sent_at_unix_ms,
        "phase": phase_for_sequence(sequence),
        "telemetry": telemetry,
        "hud": hud,
    }
    event = event_for_sequence(sequence)
    if event:
        payload["event"] = event
    return json.dumps(payload, ensure_ascii=True, separators=(",", ":")).encode("ascii")


def build_video_datagram(sequence: int, size: int = 1024) -> bytes:
    header = VIDEO_PREFIX + sequence.to_bytes(8, "big")
    if size < len(header):
        raise ValueError("video datagram size is smaller than its simulation header")
    return header + bytes((sequence + offset) % 256 for offset in range(size - len(header)))


def write_replay_header(
    stream: BinaryIO, scenario: str, tick_rate: float, video_rate: float
) -> None:
    header = {
        "format": REPLAY_FORMAT,
        "scenario": scenario,
        "tick_rate": tick_rate,
        "video_rate": video_rate,
    }
    stream.write(json.dumps(header, separators=(",", ":")).encode("ascii") + b"\n")


def write_replay_event(
    stream: BinaryIO, at_ms: int, channel: str, payload: bytes
) -> None:
    event = {
        "at_ms": at_ms,
        "channel": channel,
        "payload_b64": base64.b64encode(payload).decode("ascii"),
    }
    stream.write(json.dumps(event, separators=(",", ":")).encode("ascii") + b"\n")


def load_replay(path: Path) -> list[tuple[int, str, bytes]]:
    if path.stat().st_size > MAX_REPLAY_BYTES:
        raise ValueError("replay file exceeds the 64 MiB limit")

    with path.open("rb") as stream:
        header_line = stream.readline(MAX_REPLAY_BYTES + 1)
        try:
            header = json.loads(header_line)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError("invalid replay header") from error
        if not isinstance(header, dict) or header.get("format") != REPLAY_FORMAT:
            raise ValueError("unsupported replay format")

        events: list[tuple[int, str, bytes]] = []
        previous_at_ms = -1
        for line in stream:
            if len(events) >= MAX_REPLAY_EVENTS:
                raise ValueError("replay contains too many events")
            try:
                event = json.loads(line)
                at_ms = event["at_ms"]
                channel = event["channel"]
                payload = base64.b64decode(event["payload_b64"], validate=True)
            except (KeyError, TypeError, ValueError, UnicodeDecodeError, json.JSONDecodeError) as error:
                raise ValueError("invalid replay event") from error

            if (
                not isinstance(at_ms, int)
                or at_ms < previous_at_ms
                or at_ms > MAX_REPLAY_DURATION_MS
                or channel not in {"referee", "video"}
                or not payload
            ):
                raise ValueError("replay event is outside allowed bounds")
            if channel == "referee":
                if len(payload) > 4096:
                    raise ValueError("referee replay payload is too large")
                try:
                    decoded = json.loads(payload)
                except (UnicodeDecodeError, json.JSONDecodeError) as error:
                    raise ValueError("invalid referee replay payload") from error
                if decoded.get("channel") != CHANNEL:
                    raise ValueError("replay contains a non-simulation referee payload")
            elif len(payload) > 65507 or not payload.startswith(VIDEO_PREFIX):
                raise ValueError("replay contains an invalid simulation video payload")

            previous_at_ms = at_ms
            events.append((at_ms, channel, payload))

    if not events:
        raise ValueError("replay contains no events")
    return events


def run_live(
    ticks: int,
    tick_rate: float,
    video_rate: float,
    scenario: str,
    record_path: Path | None,
    own_team: str = "blue",
) -> None:
    if tick_rate <= 0.0 or video_rate < 0.0:
        raise ValueError("rates must be positive")

    destination = "127.0.0.1"
    video_packets_per_tick = max(0, round(video_rate / tick_rate))
    interval = 1.0 / tick_rate
    referee_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    video_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    recording = record_path.open("xb") if record_path else None
    if recording:
        write_replay_header(recording, scenario, tick_rate, video_rate)

    print(f"RM27 scenario '{scenario}' -> localhost; Ctrl+C to stop")

    sequence = 0
    deadline = time.monotonic()
    try:
        while ticks == 0 or sequence < ticks:
            elapsed_seconds = sequence / tick_rate
            referee_live, video_live = links_for_scenario(scenario, elapsed_seconds)
            sent_at_ms = time.time_ns() // 1_000_000
            event_at_ms = round(elapsed_seconds * 1000)
            if referee_live:
                referee_payload = build_referee_packet(sequence, sent_at_ms, own_team)
                referee_socket.sendto(referee_payload, (destination, REFEREE_PORT))
                if recording:
                    write_replay_event(recording, event_at_ms, "referee", referee_payload)
            if video_live:
                for video_index in range(video_packets_per_tick):
                    video_sequence = sequence * video_packets_per_tick + video_index
                    video_payload = build_video_datagram(video_sequence)
                    video_socket.sendto(
                        video_payload,
                        (destination, VIDEO_PORT),
                    )
                    if recording:
                        write_replay_event(recording, event_at_ms, "video", video_payload)

            sequence += 1
            deadline += interval
            time.sleep(max(0.0, deadline - time.monotonic()))
    except KeyboardInterrupt:
        pass
    finally:
        if recording:
            recording.close()
        referee_socket.close()
        video_socket.close()


def run_replay(path: Path, speed: float) -> None:
    if speed <= 0.0 or speed > 10.0:
        raise ValueError("replay speed must be greater than 0 and no more than 10")

    events = load_replay(path)
    sockets = {
        "referee": (socket.socket(socket.AF_INET, socket.SOCK_DGRAM), REFEREE_PORT),
        "video": (socket.socket(socket.AF_INET, socket.SOCK_DGRAM), VIDEO_PORT),
    }
    started = time.monotonic()
    print(f"Replaying {len(events)} project-simulation events from {path}")
    try:
        for at_ms, channel, payload in events:
            deadline = started + at_ms / 1000.0 / speed
            time.sleep(max(0.0, deadline - time.monotonic()))
            output_socket, port = sockets[channel]
            output_socket.sendto(
                payload,
                ("127.0.0.1", port),
            )
    except KeyboardInterrupt:
        pass
    finally:
        for output_socket, _ in sockets.values():
            output_socket.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="RM27 localhost project simulator")
    parser.add_argument("--ticks", type=int, default=0, help="referee packets; 0 runs continuously")
    parser.add_argument("--tick-rate", type=float, default=10.0, help="referee updates per second")
    parser.add_argument("--video-rate", type=float, default=30.0, help="probe datagrams per second")
    parser.add_argument("--scenario", choices=SCENARIOS, default="healthy")
    parser.add_argument("--team", choices=("red", "blue"), default="blue",
                        help="friendly team for simulated HUD")
    parser.add_argument("--record", type=Path, help="write a bounded simulation replay; requires --ticks")
    parser.add_argument("--replay", type=Path, help="replay a previously recorded simulation file")
    parser.add_argument("--speed", type=float, default=1.0, help="replay speed, from 0 to 10")
    return parser.parse_args()


def main() -> None:
    arguments = parse_args()
    if arguments.record and arguments.replay:
        raise SystemExit("--record and --replay cannot be used together")
    if arguments.record and arguments.ticks <= 0:
        raise SystemExit("--record requires a finite --ticks value")
    if arguments.ticks < 0:
        raise SystemExit("--ticks cannot be negative")
    if arguments.replay:
        run_replay(arguments.replay, arguments.speed)
    else:
        run_live(
            arguments.ticks,
            arguments.tick_rate,
            arguments.video_rate,
            arguments.scenario,
            arguments.record,
            arguments.team,
        )


if __name__ == "__main__":
    main()
