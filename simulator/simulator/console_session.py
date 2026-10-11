"""Bounded, loopback-only simulation session, scenes and packet recording."""

import copy
import heapq
import json
import queue
import random
import socket
import threading
import time
from collections import deque
from pathlib import Path

from simulator.rm27_simulator import (
    MAX_REPLAY_BYTES, MAX_REPLAY_EVENTS, MAX_REPLAY_DURATION_MS,
    REFEREE_PORT, VIDEO_PORT, build_video_datagram, load_replay, write_replay_event,
)
from simulator.sim_mqtt_broker import ConsoleBroker
from simulator.sim_state import (
    SCENE_FORMAT, default_state, number, state_packet, validate_event, validate_state,
)
from simulator.stream_video import PORT as TS_PORT, PACKET_BYTES, validate_sample


def default_settings():
    return dict(transport="udp", referee_enabled=True, mqtt_enabled=True,
                video_enabled=True, video_mode="none", video_path="", bitrate_kbps=500,
                loss_pct=0.0, delay_ms=0, jitter_ms=0, reorder_pct=0.0,
                corrupt_pct=0.0, seed=27)


def default_scene():
    return dict(format=SCENE_FORMAT, state=default_state(), settings=default_settings(), steps=[])


def validate_settings(settings):
    if not isinstance(settings, dict) or set(settings) != set(default_settings()):
        raise ValueError("联调设置字段不完整或含未知字段")
    if settings["transport"] not in ("udp", "mqtt"):
        raise ValueError("裁判链路只能为 udp 或 mqtt")
    if settings["video_mode"] not in ("none", "probe", "ts"):
        raise ValueError("视频模式只能为 none / probe / ts")
    for key in ("referee_enabled", "mqtt_enabled", "video_enabled"):
        if type(settings[key]) is not bool:
            raise ValueError(f"{key} 必须为布尔值")
    path = settings["video_path"]
    if (not isinstance(path, str) or len(path) > 1024 or "\0" in path
            or "://" in path or path.startswith(("//", "\\\\"))):
        raise ValueError("视频只能选择本地文件，不能使用 URL 或网络共享")
    number(settings["bitrate_kbps"], 64, 20000, True)
    number(settings["seed"], 0, 2**32 - 1, True)
    number(settings["delay_ms"], 0, 1000, True)
    number(settings["jitter_ms"], 0, 500, True)
    for key in ("loss_pct", "reorder_pct", "corrupt_pct"):
        number(settings[key], 0, 100)
    return copy.deepcopy(settings)


def validate_scene(scene):
    if (not isinstance(scene, dict) or set(scene) != {"format", "state", "settings", "steps"}
            or scene["format"] != SCENE_FORMAT):
        raise ValueError("不是 rm27.sim.scene.v1 场景")
    validate_state(scene["state"])
    validate_settings(scene["settings"])
    steps = scene["steps"]
    if not isinstance(steps, list) or len(steps) > 1000:
        raise ValueError("场景最多 1000 步")
    previous = -1
    for step in steps:
        if not isinstance(step, dict) or not {"at_ms", "changes"} <= set(step) <= {
                "at_ms", "changes", "event"}:
            raise ValueError("场景步骤必须包含 at_ms 和 changes")
        number(step["at_ms"], 0, MAX_REPLAY_DURATION_MS, True)
        if step["at_ms"] < previous:
            raise ValueError("场景时间必须递增")
        previous = step["at_ms"]
        changes = step["changes"]
        if not isinstance(changes, dict) or not set(changes) <= {
                "state", "referee_enabled", "video_enabled", "mqtt_enabled"}:
            raise ValueError("步骤只允许替换状态或切换三种链路开关")
        if "state" in changes:
            validate_state(changes["state"])
        for key in set(changes) - {"state"}:
            if type(changes[key]) is not bool:
                raise ValueError("步骤中的开关必须为布尔值")
        if "event" in step:
            validate_event(step["event"])
    return copy.deepcopy(scene)


def scenario_steps(name):
    switches = {
        "正常": [],
        "裁判过期": [(3000, "referee_enabled", False), (6000, "referee_enabled", True)],
        "独立断流恢复": [(3000, "referee_enabled", False), (5000, "referee_enabled", True),
                         (7000, "video_enabled", False), (9000, "video_enabled", True)],
        "MQTT 断线重连": [(3000, "mqtt_enabled", False), (6000, "mqtt_enabled", True)],
    }
    return [dict(at_ms=at, changes={key: value}) for at, key, value in switches[name]]


class VideoFaultQueue:
    """Seeded datagram impairment. Max 256 datagrams, stale queues are discarded."""

    def __init__(self, settings):
        self.settings = settings
        self.random = random.Random(settings["seed"])
        self.pending = []
        self.ordinal = 0
        self.dropped = self.corrupted = self.reordered = 0

    def enqueue(self, now, payload):
        cfg = self.settings
        self.ordinal += 1
        if self.random.random() * 100 < cfg["loss_pct"] or len(self.pending) >= 256:
            self.dropped += 1
            return
        if self.random.random() * 100 < cfg["corrupt_pct"]:
            data = bytearray(payload)
            data[self.random.randrange(len(data))] ^= 0x01
            payload = bytes(data)
            self.corrupted += 1
        delay = cfg["delay_ms"] + self.random.uniform(0, cfg["jitter_ms"])
        if self.random.random() * 100 < cfg["reorder_pct"]:
            delay += 60
            self.reordered += 1
        heapq.heappush(self.pending, (now + delay / 1000, self.ordinal, payload))

    def due(self, now):
        while self.pending and self.pending[0][0] <= now:
            yield heapq.heappop(self.pending)[2]

    def clear(self):
        self.dropped += len(self.pending)
        self.pending.clear()


class Recorder:
    def __init__(self, path):
        self.stream = Path(path).open("xb")  # Never overwrite an existing recording.
        self.events = 0
        self.last_at_ms = 0
        self.stream.write(b'{"format":"rm27.sim.replay.v2"}\n')

    def write(self, at_ms, channel, payload):
        # Operations within one tick can precede a send stamped at the tick boundary.
        at_ms = max(at_ms, self.last_at_ms)
        # JSON/base64 overhead is bounded by this conservative estimate.
        if (self.events >= MAX_REPLAY_EVENTS or at_ms > MAX_REPLAY_DURATION_MS
                or self.stream.tell() + len(payload) * 2 + 256 > MAX_REPLAY_BYTES):
            raise ValueError("录制达到 64 MiB / 200000 条 / 24 小时上限，已停止")
        write_replay_event(self.stream, at_ms, channel, payload)
        self.last_at_ms = at_ms
        self.events += 1

    def close(self):
        self.stream.close()


class ConsoleSession:
    def __init__(self, *, referee_port=REFEREE_PORT, video_port=VIDEO_PORT,
                 ts_port=TS_PORT, mqtt_port=39004):
        # Tests inject OS-assigned ports; destinations remain strictly loopback.
        for port in (referee_port, video_port, ts_port):
            number(port, 1, 65535, True)
        number(mqtt_port, 0, 65535, True)
        self.referee_port, self.video_port, self.ts_port = referee_port, video_port, ts_port
        self.mqtt_port = mqtt_port
        self.commands = queue.Queue(maxsize=128)
        self.messages = queue.Queue(maxsize=500)
        self.stop_event = threading.Event()
        self.worker = None
        self.snapshot = {}
        self.sequence = 0

    @property
    def running(self):
        return self.worker is not None and self.worker.is_alive()

    def log(self, text):
        try:
            self.messages.put_nowait(text)
        except queue.Full:
            try:
                self.messages.get_nowait()
            except queue.Empty:
                pass
            try:
                self.messages.put_nowait(text)
            except queue.Full:
                pass

    def start(self, scene, record=None, replay=None, speed=1.0):
        if self.running:
            raise ValueError("请先停止当前联调")
        scene = validate_scene(scene)
        number(speed, 0.1, 10)
        while not self.commands.empty():
            self.commands.get_nowait()
        self.stop_event.clear()
        self.snapshot = dict(replay=bool(replay), elapsed_ms=0)
        self.worker = threading.Thread(target=self._run, args=(scene, record, replay, speed),
                                       daemon=True)
        self.worker.start()

    def post(self, kind, value):
        if not self.running:
            raise ValueError("请先启动联调")
        if self.snapshot.get("replay", False):
            raise ValueError("回放时不能注入手动操作")
        if kind == "scene":
            value = validate_scene(value)
        elif kind == "event":
            value = validate_event(value)
        elif kind == "switch":
            if (not isinstance(value, dict) or not set(value) <= {
                    "referee_enabled", "video_enabled", "mqtt_enabled"}
                    or any(type(v) is not bool for v in value.values())):
                raise ValueError("链路开关无效")
        else:
            raise ValueError("未知模拟操作")
        try:
            self.commands.put_nowait((kind, copy.deepcopy(value)))
        except queue.Full as error:
            raise ValueError("操作过快，队列已满；请稍后重试") from error

    def stop(self):
        self.stop_event.set()

    def _run(self, scene, record_path, replay_path, speed):
        broker = recorder = stream = None
        try:
            settings = scene["settings"]
            if settings["transport"] == "mqtt":
                broker = ConsoleBroker(self.log, port=self.mqtt_port)
                self.mqtt_port = broker.server.getsockname()[1]
                broker.set_enabled(settings["mqtt_enabled"])
            if replay_path:
                self._replay(Path(replay_path), speed, broker)
                return
            if settings["video_mode"] == "ts":
                validate_sample(Path(settings["video_path"]))
                stream = Path(settings["video_path"]).open("rb")
            if record_path:
                recorder = Recorder(record_path)
            state = scene["state"]
            faults = VideoFaultQueue(settings)
            started = next_referee = next_video = last_clock = time.monotonic()
            clock_fraction = 0.0
            step_index = probe_sequence = 0
            event = None
            event_repeats = 0
            pending_events = deque()
            sent = dict(referee=0, video=0)
            self.log("联调启动：仅 127.0.0.1；模拟与回放禁止真实指令下发")

            def note(text):
                nonlocal recorder
                self.log(text)
                if recorder:
                    payload = json.dumps(dict(format="rm27.sim.control.v1", text=text,
                                              state=state, settings=settings),
                                         ensure_ascii=True).encode("ascii")
                    recorder.write(round((time.monotonic() - started) * 1000), "control", payload)

            def apply(changes):
                nonlocal state, event, event_repeats
                video_was_enabled = settings["video_enabled"]
                if "state" in changes:
                    state = copy.deepcopy(changes["state"])
                for key in set(changes) - {"state"}:
                    settings[key] = changes[key]
                if not settings["video_enabled"]:
                    faults.clear()
                elif not video_was_enabled and stream:
                    # Resume from TS headers/keyframe instead of flushing pre-outage bytes.
                    stream.seek(0)
                if broker:
                    broker.set_enabled(settings["mqtt_enabled"])
                if not settings["referee_enabled"] or (broker and not settings["mqtt_enabled"]):
                    event, event_repeats = None, 0
                    pending_events.clear()

            def queue_event(value):
                if not settings["referee_enabled"] or (broker and not settings["mqtt_enabled"]):
                    note("裁判链路已关闭，模拟事件未排队")
                elif len(pending_events) >= 32:
                    note("模拟事件队列已满，本条未发送")
                else:
                    pending_events.append(value)

            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                note("场景开始")
                while not self.stop_event.is_set():
                    now = time.monotonic()
                    elapsed_ms = round((now - started) * 1000)
                    dt, last_clock = now - last_clock, now
                    if not state["paused"] and state["phase"] != "ended":
                        clock_fraction += dt
                        seconds = int(clock_fraction)
                        clock_fraction -= seconds
                        state["remaining_seconds"] = max(0, state["remaining_seconds"] - seconds)
                    while step_index < len(scene["steps"]) and scene["steps"][step_index]["at_ms"] <= elapsed_ms:
                        step = scene["steps"][step_index]
                        apply(step["changes"])
                        if "event" in step:
                            queue_event(step["event"])
                        note(f"执行场景步骤 {step_index + 1}")
                        step_index += 1
                    while True:
                        try:
                            kind, value = self.commands.get_nowait()
                        except queue.Empty:
                            break
                        if kind == "event":
                            queue_event(value)
                            note(f"请求模拟事件：{value['text']}")
                        elif kind == "switch":
                            apply(value)
                            note(f"链路开关：{value}")
                        elif kind == "scene":
                            new_settings = value["settings"]
                            if any(new_settings[k] != settings[k] for k in (
                                    "transport", "video_mode", "video_path")):
                                self.log("更换通信/视频源请先停止；当前操作未应用")
                                continue
                            state = value["state"]
                            old_settings = copy.deepcopy(settings)
                            settings = new_settings
                            # apply() needs the old switch values to detect a resume.
                            switches = {key: settings[key] for key in (
                                "referee_enabled", "video_enabled", "mqtt_enabled")}
                            for key in switches:
                                settings[key] = old_settings[key]
                            if any(settings[k] != old_settings[k] for k in (
                                    "loss_pct", "delay_ms", "jitter_ms", "reorder_pct", "corrupt_pct", "seed")):
                                faults.clear()
                                faults = VideoFaultQueue(settings)
                            else:
                                faults.settings = settings
                            apply(switches)
                            note("应用手动状态与故障参数（计划步骤不重新开始）")
                    if now >= next_referee:
                        # No accumulated burst after a slow operation or reconnection.
                        next_referee = now + 0.1
                        if event_repeats == 0 and pending_events:
                            event, event_repeats = pending_events.popleft(), 5
                        payload = state_packet(state, self.sequence, time.time_ns() // 1000000,
                                               event if event_repeats else None)
                        self.sequence += 1
                        delivered = False
                        if settings["referee_enabled"]:
                            if broker:
                                delivered = broker.publish(payload)
                            else:
                                sender.sendto(payload, ("127.0.0.1", self.referee_port))
                                delivered = True
                        if delivered:
                            sent["referee"] += 1
                            if event_repeats:
                                event_repeats -= 1
                            if recorder:
                                recorder.write(elapsed_ms, "referee", payload)
                        elif broker:
                            # Never hold a manual event for a later MQTT session.
                            event, event_repeats = None, 0
                            pending_events.clear()
                    mode = settings["video_mode"]
                    if settings["video_enabled"] and mode != "none" and now >= next_video:
                        if stream:
                            data = stream.read(PACKET_BYTES)
                            if not data:
                                stream.seek(0)
                                data = stream.read(PACKET_BYTES)
                            interval = len(data) * 8 / (settings["bitrate_kbps"] * 1000)
                        else:
                            data = build_video_datagram(probe_sequence)
                            probe_sequence += 1
                            interval = 1 / 30
                        next_video = now + interval
                        faults.enqueue(now, data)
                    for data in faults.due(now):
                        port = self.ts_port if mode == "ts" else self.video_port
                        sender.sendto(data, ("127.0.0.1", port))
                        sent["video"] += 1
                        if recorder:
                            # Impaired TS may be corrupt. Keep raw faults in a separate channel.
                            channel = "video_ts_raw" if mode == "ts" else "video_probe_raw"
                            recorder.write(elapsed_ms, channel, data)
                    self.snapshot = dict(state=copy.deepcopy(state), settings=copy.deepcopy(settings),
                                         elapsed_ms=elapsed_ms, sent=sent.copy(), queued=len(faults.pending),
                                         dropped=faults.dropped, corrupted=faults.corrupted,
                                         reordered=faults.reordered, replay=False)
                    self.stop_event.wait(0.005)
                faults.clear()
                note("场景结束；未发送的旧视频队列已丢弃")
        except (OSError, ValueError, KeyError, TypeError) as error:
            self.log(f"联调停止：{error}")
        finally:
            if stream:
                stream.close()
            if recorder:
                recorder.close()
            if broker:
                broker.close()
            self.log("后台会话已结束，端口已释放")

    def _replay(self, path, speed, broker):
        events = load_replay(path)
        self.log(f"回放 {len(events)} 条发送端记录；先静默 800 ms，清除上一会话新鲜度")
        if self.stop_event.wait(0.8):
            return
        started = time.monotonic()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            for at_ms, channel, payload in events:
                if self.stop_event.wait(max(0, started + at_ms / 1000 / speed - time.monotonic())):
                    return
                if channel == "control":
                    note = json.loads(payload)
                    self.log("回放记录：" + note["text"])
                    if broker and "settings" in note:
                        broker.set_enabled(note["settings"]["mqtt_enabled"])
                elif channel == "referee" and broker:
                    if not broker.publish(payload):
                        self.log("回放裁判包未送达：MQTT 尚未订阅")
                else:
                    port = {"referee": self.referee_port, "video": self.video_port,
                            "video_ts": self.ts_port, "video_ts_raw": self.ts_port,
                            "video_probe_raw": self.video_port}[channel]
                    sender.sendto(payload, ("127.0.0.1", port))
                self.snapshot = dict(replay=True, elapsed_ms=at_ms)
        self.log("回放结束；控制记录仅供显示，不执行其中的文件或配置操作")
