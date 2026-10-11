"""Bounded synthetic loopback wire scenario, not a broker or referee simulator.

Artificial binary fixtures follow the project's historical client.proto.
No official captures, outbound command handling, sim.v1 or external endpoints.
"""

import argparse
from contextlib import suppress
from datetime import datetime
import json
import os
from pathlib import Path
import re
import select
import socket
import struct
import subprocess
import sys
import threading
import time
import uuid

from mqtt_wire import packet, publish, read_packet

TOPICS = sorted(("Buff", "CustomByteBlock", "DeployModeStatusSync", "Event", "GameStatus", "GlobalLogisticsStatus",
                 "GlobalUnitStatus", "PenaltyInfo", "RobotDynamicStatus", "RobotModuleStatus",
                 "RobotPosition", "RobotRespawnStatus", "RobotStaticStatus", "RobotPerformanceSelectionSync"))

# Values are illustrative, not official limits, game events or match recordings.
FIXTURES = {
    "RobotStaticStatus": bytes.fromhex("20 65 40 03 48 e8 07"),
    "RobotDynamicStatus": b"\x08\xd2\x06\x15" + struct.pack("<f", 20.0) +
        b"\x1d" + struct.pack("<f", 24.0) + b"\x48\x50",
    "GlobalUnitStatus": bytes.fromhex("08 88 27 20 dc 0b 28 00 30 a0 1f 48 00 50 03 5a 14") +
        bytes.fromhex("e8 07") * 10,
    "GlobalLogisticsStatus": b"\x08\x64",
    "GameStatus": bytes.fromhex("08 01 10 03 18 00 20 02 28 04 30 a4 03 38 00 40 00"),
    "PenaltyInfo": bytes.fromhex("08 01 10 00 18 01"),
    "RobotModuleStatus": bytes.fromhex("08 01 10 00 18 01 20 01 28 02 30 01 38 01 40 01 48 01 50 01 58 01"),
    "RobotRespawnStatus": bytes.fromhex("08 00 10 64 18 00 20 00 28 0a 30 00"),
    "Event": b"\x08\x00\x12\x14synthetic-local-demo",
    "RobotPosition": b"\x0d" + struct.pack("<f", 2.5) + b"\x15" + struct.pack("<f", -1.0) +
        b"\x25" + struct.pack("<f", 30.0) + b"\x28\x6b",
    "Buff": bytes.fromhex("08 65 10 02 18 ff ff ff ff ff ff ff ff ff 01 20 0a 28 00"),
    "RobotPerformanceSelectionSync": bytes.fromhex("08 00 10 02 18 00"),
    "DeployModeStatusSync": bytes.fromhex("08 00"),
}


def handshake(server, robot_id=None):
    stream, _ = server.accept()
    try:
        stream.settimeout(2)
        header, body = read_packet(stream)
        identity = body[12:]
        if header != 0x10 or body[:10] != b"\x00\x04MQTT\x04\x02\x00\x0a" or len(body) < 12 or \
                struct.unpack("!H", body[10:12])[0] != len(identity) or \
                (identity != str(robot_id).encode() if robot_id is not None else
                 not identity.startswith(b"rm2026-local-observer-")):
            raise ValueError("Only the historical local observer is supported")
        stream.sendall(b"\x20\x02\x00\x00")
        for topic in TOPICS:
            header, body = read_packet(stream)
            expected = struct.pack("!H", len(topic)) + topic.encode() + b"\x01"
            if header != 0x82 or len(body) < 2 or body[2:] != expected:
                raise ValueError("Expected exact read-only subscription, no client PUBLISH")
            stream.sendall(packet(0x90, body[:2] + b"\x01"))
        return stream
    except BaseException:
        stream.close()
        raise


def quiet(stream, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if select.select([stream], [], [], min(0.1, max(0, deadline - time.monotonic())))[0]:
            if read_packet(stream) != (0xc0, b""):
                raise ValueError("Unexpected client packet during silence; no publishing allowed")
            stream.sendall(b"\xd0\x00")


def hevc_frames(path):
    if not path.is_file() or not 1 <= path.stat().st_size <= 8 * 1024 * 1024:
        raise ValueError("HEVC fixture must be a local Annex-B file, at most 8 MiB")
    with path.open("rb") as source:
        data = source.read(8 * 1024 * 1024 + 1)
    starts, types = [], set()
    for match in re.finditer(b"\x00\x00(?:\x00)?\x01", data):
        if match.end() + 2 > len(data):
            raise ValueError("Truncated HEVC NAL header")
        kind = (data[match.end()] >> 1) & 63
        types.add(kind)
        if kind == 35:  # Only generated fixtures with explicit access-unit delimiters.
            starts.append(match.start())
    if len(data) > 8 * 1024 * 1024 or len(starts) < 30 or starts[0] != 0 or \
            not {32, 33, 34}.issubset(types) or not types.intersection({19, 20}):
        raise ValueError("Expected generated HEVC with AUDs, VPS/SPS/PPS and IDR")
    frames = [data[start:end] for start, end in zip(starts, starts[1:] + [len(data)])]
    if any(len(frame) > 1024 * 1024 for frame in frames):
        raise ValueError("HEVC fixture frame exceeds 1 MiB")
    return frames


def run(executable, phase_seconds, video_ts=None, diagnostic=lambda message: None,
        hevc_file=None, robot_id=None, fragment_base=0, video_faults=False):
    frames = hevc_frames(hevc_file) if hevc_file else None
    has_video = bool(video_ts or frames)
    started = time.monotonic()
    latest = {}
    lock = threading.Lock()
    process = None
    stream = None
    video_enabled = threading.Event()
    video_damaged = threading.Event()
    video_stop = threading.Event()
    video_errors = []
    sender = None
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))  # Owned ephemeral port, never take over 39004/39005.
        server.listen(1)
        server.settimeout(8)
        port = server.getsockname()[1]
        command = [str(executable), "--protocol", "rm2026-v2.0.0",
            "--port", str(port), "--synthetic-demo", "--duration-ms", "60000"]
        if robot_id is not None:
            command += ["--robot-id", str(robot_id)]
        if has_video:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
                reservation.bind(("127.0.0.1", 0))
                video_port = reservation.getsockname()[1]
            command += (["--hevc-video-port", str(video_port), "--fragment-base", str(fragment_base)]
                        if frames else ["--sim-video-port", str(video_port)])
        process = subprocess.Popen(command,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")

        def collect_errors():
            for line in process.stderr:
                diagnostic("CHILD STDERR " + line.rstrip())

        def collect():
            for line in process.stdout:
                try:
                    report = json.loads(line)
                except json.JSONDecodeError:
                    with lock:
                        latest.clear()
                        latest["collector_error"] = "Invalid observer diagnostic JSON"
                    return
                with lock:
                    latest.clear()
                    latest.update(report)  # Only latest diagnostic report; no history queue.

        def send_video():
            # Project test format only. Stream at 500 kbps without a frame/backlog queue.
            try:
                with video_ts.open("rb") as source, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                    deadline = time.monotonic()
                    while not video_stop.is_set():
                        if not video_enabled.is_set():
                            # Keep the TS timeline; decoder resumes at the next keyframe.
                            deadline = time.monotonic()
                            video_stop.wait(0.02)
                            continue
                        data = source.read(1316)
                        if not data:
                            return  # Single pass: never wrap TS timestamps backwards.
                        udp.sendto(data, ("127.0.0.1", video_port))
                        deadline += len(data) * 8 / 500000
                        video_stop.wait(max(0, deadline - time.monotonic()))
            except (OSError, ValueError) as error:
                video_errors.append(str(error))

        def send_hevc():
            # Generated 30 fps test pattern only; this is not a device or pairing emulator.
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                    sequence, index = 0, 0
                    was_damaged = False
                    deadline = time.monotonic()
                    while not video_stop.is_set():
                        if not video_enabled.is_set():
                            index = 0  # Resume the synthetic pattern at its initial IDR.
                            deadline = time.monotonic()
                            video_stop.wait(0.02)
                            continue
                        damaged = video_damaged.is_set()
                        if damaged != was_damaged:
                            index = 0  # Each transition begins at a generated IDR.
                        frame = frames[index]
                        if damaged and not was_damaged:
                            # Valid transport header, invalid HEVC forbidden_zero_bit.
                            frame = b"\x00\x00\x00\x01\x80\x01\x2a"
                        fragments = [struct.pack("!HHI", sequence, offset // 1392 + fragment_base, len(frame))
                            + frame[offset:offset + 1392] for offset in range(0, len(frame), 1392)]
                        if damaged:
                            udp.sendto(b"broken", ("127.0.0.1", video_port))  # Truncated header.
                            if any(16 <= (frame[match.end()] >> 1) & 63 <= 21 for match in
                                   re.finditer(b"\x00\x00(?:\x00)?\x01", frame)):
                                fragments = fragments[1:]  # Lose one slice of each recovery keyframe.
                        for fragment in reversed(fragments):
                            udp.sendto(fragment, ("127.0.0.1", video_port))
                            if len(fragments) > 1 and sequence % 10 == 0:
                                udp.sendto(fragment, ("127.0.0.1", video_port))
                        sequence = (sequence + 1) & 65535
                        index = (index + 1) % len(frames)
                        was_damaged = damaged
                        deadline = max(deadline + 1 / 30, time.monotonic())
                        video_stop.wait(max(0, deadline - time.monotonic()))
            except (OSError, ValueError) as error:
                video_errors.append(str(error))

        reader = threading.Thread(target=collect, daemon=True)
        error_reader = threading.Thread(target=collect_errors, daemon=True)
        reader.start()
        error_reader.start()

        def wait_report(predicate):
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline:
                with lock:
                    report = dict(latest)
                if "collector_error" in report:
                    raise RuntimeError(report["collector_error"])
                if video_errors:
                    raise RuntimeError("Video sender failed: " + video_errors[0])
                if predicate(report):
                    return report
                if process.poll() is not None:
                    raise RuntimeError("Observer closed before scenario completed")
                time.sleep(0.02)
            raise RuntimeError("Observer did not reach expected scenario state")

        def announce(phase, report):
            diagnostic("PHASE " + phase + " link=" + str(report.get("link")))
            output = {"synthetic": True, "phase": phase, "port": port,
                "elapsed_ms": int((time.monotonic() - started) * 1000),
                "link": report.get("link"), "state": report.get("state", {}),
                "stale": report.get("stale", [])}
            if "client_id" in report:
                output["client_id"] = report["client_id"]
            if "hud" in report:
                output["hud"] = report["hud"]
            if "video" in report:
                output["video"] = report["video"]
            print(json.dumps(output, ensure_ascii=True), flush=True)

        def send_phase(recovered=False, video_ready=lambda report: True):
            deadline = time.monotonic() + phase_seconds
            video_deadline = time.monotonic() + 15
            report = {}
            while time.monotonic() < deadline or not video_ready(report):
                if time.monotonic() > video_deadline:
                    raise RuntimeError("Video did not reach expected decoded/stale state")
                for mid, (topic, payload) in enumerate(FIXTURES.items(), 1):
                    if recovered and topic == "RobotStaticStatus":
                        payload = bytes.fromhex("20 01 40 02 48 e8 07")  # New red identity.
                    if recovered and topic == "RobotDynamicStatus":
                        payload = bytes.fromhex("08 f4 03")  # HP 500; old heat/shots must disappear.
                    if recovered and topic == "Buff":
                        payload = bytes.fromhex("18 00")  # No ID/type fallback from the previous session.
                    if recovered and topic == "RobotPerformanceSelectionSync":
                        payload = bytes.fromhex("18 63")
                    if recovered and topic == "DeployModeStatusSync":
                        payload = bytes.fromhex("08 63")
                    if recovered and topic == "GameStatus":
                        payload = bytes.fromhex("28 04")  # Phase only; old score/time/pause must disappear.
                    publish(stream, topic, payload, mid=mid)
                    if read_packet(stream) != (0x40, struct.pack("!H", mid)):
                        raise ValueError("Expected PUBACK, not client publication")
                report = wait_report(lambda value: len(value.get("state", {})) == len(FIXTURES) and
                    value["state"].get("self_config", {}).get("robot_id") == (1 if recovered else 101))
                if not recovered and report["state"]["self_dynamic"].get("health") != 850:
                    raise RuntimeError("Synthetic fixture decode mismatch")
                if recovered and report["state"]["self_dynamic"] != {"health": 500}:
                    raise RuntimeError("Old dynamic fields survived recovery")
                quiet(stream, 0.5)
            return report

        try:
            stream = handshake(server, robot_id)
            wait_report(lambda value: value.get("link") == "subscribed" and value.get("state") == {})
            if has_video:
                video_enabled.set()
                sender = threading.Thread(target=send_hevc if frames else send_video, daemon=True)
                sender.start()
            announce("normal", send_phase(video_ready=lambda value: not has_video or
                (value.get("video", {}).get("fresh") and value["video"].get("decodedFrames", 0) > 5)))
            if has_video:
                video_enabled.clear()
                report = send_phase(video_ready=lambda value: value.get("video", {}).get("stale") and
                    not value["video"]["fresh"] and value["video"]["decodedFrames"] > 5)
                announce("video_expired", report)  # Referee packets continue throughout this fault.
                last_frames = report["video"]["decodedFrames"]
                video_enabled.set()
                announce("video_recovered", send_phase(video_ready=lambda value:
                    value.get("video", {}).get("fresh") and not value["video"]["stale"] and
                    value["video"]["decodedFrames"] > last_frames + 5))
            if video_faults:
                video_damaged.set()
                report = send_phase(video_ready=lambda value: value.get("video", {}).get("stale") and
                    value["video"]["transport"].get("waitingForKeyframe") and
                    value["video"]["transport"].get("invalidHevcUnits", 0) > 0 and
                    value["video"]["transport"].get("droppedFrames", 0) > 0)
                last_frames = report["video"]["decodedFrames"]
                last_packets = report["video"]["transport"]["packets"]
                quiet(stream, 0.5)
                report = wait_report(lambda value: value.get("video", {}).get("stale") and
                    value["video"]["decodedFrames"] == last_frames and
                    value["video"]["transport"]["packets"] > last_packets)
                announce("video_damaged", report)  # Incoming packets must not refresh old pixels.
                video_damaged.clear()
                announce("video_keyframe_recovered", send_phase(video_ready=lambda value:
                    value.get("video", {}).get("fresh") and
                    not value["video"]["transport"].get("waitingForKeyframe") and
                    value["video"]["decodedFrames"] > last_frames + 5))
            quiet(stream, 2)  # Exceeds the project's 1500 ms reception window.
            report = wait_report(lambda value: value.get("link") == "subscribed" and
                value.get("state") == {} and len(value.get("stale", [])) == len(FIXTURES))
            if has_video and not report.get("video", {}).get("fresh"):
                raise RuntimeError("Video fixture ended or stalled before independent data-expiry check")
            announce("expired", report)
            # Independent failure checks are complete; the remaining steps test MQTT reconnect only.
            if not frames:
                video_stop.set()
            quiet(stream, phase_seconds)
            stream.close()
            stream = None
            report = wait_report(lambda value: value.get("link") == "offline" and
                value.get("state") == {} and value.get("stale") == [])
            if frames and not report.get("video", {}).get("fresh"):
                raise RuntimeError("MQTT disconnect incorrectly stalled independent video")
            announce("offline", report)
            # The observer must reconnect and re-SUBACK all eleven topics.
            stream = handshake(server, robot_id)
            wait_report(lambda value: value.get("link") == "subscribed" and value.get("state") == {})
            announce("recovered", send_phase(recovered=True))
            announce("complete", {"link": "scenario-finished"})
        finally:
            video_stop.set()
            if sender is not None:
                sender.join(timeout=3)
            if stream is not None:
                stream.close()
            if process.poll() is None:
                process.terminate()  # Only the child launched by this short-lived scenario.
            process.wait(timeout=3)
            reader.join(timeout=3)
            error_reader.join(timeout=3)
            process.stdout.close()
            process.stderr.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True, help="Built historical read-only HUD or viewer")
    parser.add_argument("--phase-seconds", type=int, choices=range(2, 7), default=4)
    parser.add_argument("--video-ts", type=Path, help="Optional generated MPEG-TS fixture (not official video)")
    parser.add_argument("--hevc-file", type=Path, help="Generated 30 fps Annex-B HEVC with AUDs (not real hardware)")
    parser.add_argument("--fragment-base", type=int, choices=(0, 1), default=0)
    parser.add_argument("--video-faults", action="store_true", help="Damage synthetic HEVC while MQTT continues")
    parser.add_argument("--robot-id", choices=tuple(str(value) for value in
        (1, 2, 3, 4, 6, 7, 101, 102, 103, 104, 106, 107)))
    args = parser.parse_args()
    if not args.exe.is_file():
        parser.error("Build the viewer first; executable does not exist")
    if args.video_ts and args.hevc_file:
        parser.error("Choose one video format, not both")
    if args.video_faults and not args.hevc_file:
        parser.error("Video faults require a generated HEVC fixture")
    if args.hevc_file:
        try:
            hevc_frames(args.hevc_file)
        except (OSError, ValueError) as error:
            parser.error(str(error))
    if args.video_ts:
        if not args.video_ts.is_file() or not 188 <= args.video_ts.stat().st_size <= 8 * 1024 * 1024:
            parser.error("Video fixture must be a local MPEG-TS file, at most 8 MiB")
        # Bounded validation before launching a GUI; codec support is checked by actual decoding.
        with args.video_ts.open("rb") as source:
            data = source.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024 or len(data) % 188 or any(
                data[index] != 0x47 for index in range(0, len(data), 188)):
            parser.error("Expected 188-byte MPEG-TS packets, not an arbitrary media file")
    log = None
    log_lock = threading.Lock()
    root = Path(os.environ.get("RM27_ARTIFACT_ROOT", str(Path(__file__).resolve().parents[1])))
    if not root.is_absolute():
        root = Path(__file__).resolve().parents[1]
    try:
        directory = root / "log"
        if directory.is_symlink() or directory.resolve().parent != root.resolve():
            raise OSError("Refusing redirected log directory")
        directory.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S%f")[:-3]
        path = directory / f"rm27-demo-{stamp}-{os.getpid()}-{uuid.uuid4().hex}.log"
        log = path.open("xb")
        log.write(b"RM27-RUNTIME-LOG v1\nSTART synthetic loopback demo\n")
        log.flush()
    except OSError as error:
        if log is not None:
            with suppress(OSError):
                log.close()
            log = None
        print("Demo log unavailable: " + str(error), file=sys.stderr)

    def diagnostic(message):
        nonlocal log
        line = (datetime.now().isoformat(timespec="milliseconds") + " " +
            message[:2048].replace("\r", "\\r").replace("\n", "\\n") + "\n").encode("utf-8")
        with log_lock:
            if log is None:
                return
            try:
                if log.tell() + len(line) <= 4 * 1024 * 1024:
                    log.write(line)
                    log.flush()
            except OSError as error:
                with suppress(OSError):
                    log.close()
                log = None
                print("Demo log disabled: " + str(error), file=sys.stderr)

    try:
        run(args.exe.resolve(), args.phase_seconds, args.video_ts, diagnostic,
            args.hevc_file, args.robot_id, args.fragment_base, args.video_faults)
    except KeyboardInterrupt:
        diagnostic("INTERRUPTED")
        return 130
    except (OSError, EOFError, ValueError, RuntimeError) as error:
        diagnostic("FAILED " + str(error))
        print("Synthetic local demo failed: " + str(error), file=sys.stderr)
        return 1
    finally:
        diagnostic("END")
        if log is not None:
            log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
