"""Canonical project-only match state. No official control or rule semantics."""

import copy
import json
import math
from pathlib import Path

PHASES = ("preparation", "self_check", "countdown", "match", "ended")
ROLES = ("哨兵", "无人机", "步兵3", "步兵4", "工程", "英雄")
SCENE_FORMAT = "rm27.sim.scene.v1"


def number(value, minimum, maximum, integer=False):
    if (type(value) not in (int, float) or not math.isfinite(value)
            or not minimum <= value <= maximum or (integer and type(value) is not int)):
        raise ValueError(f"需要 {'整数' if integer else '数值'} {minimum}–{maximum}: {value!r}")
    return value


def default_state():
    robots = {}
    for team in ("red", "blue"):
        robots[team] = [dict(hp=1000, max_hp=1000, shield=100, max_shield=150,
                            level=3, ammo=200, position=[round(0.12 + i * 0.12, 2),
                                                       0.25 if team == "blue" else 0.75])
                        for i in range(6)]
    return dict(team="blue", self_index=5, phase="match", remaining_seconds=420,
                paused=True, robots=robots, coins=dict(red=80, blue=100),
                structures={team: dict(base=dict(hp=5000, max_hp=5000),
                                       outpost=dict(hp=1500, max_hp=1500))
                            for team in ("red", "blue")},
                restart_count=2, pitch_deg=0.0, yaw_deg=0.0, speed_mps=16.0,
                allowed_shots=80, chassis_power_w=80.0, buffer_energy_j=48.0,
                barrel_heat=0, actions=dict(climb=False, fire=False, speed_up=False))


def validate_state(state):
    required = set(default_state()) - {"structures"}
    if not isinstance(state, dict) or set(state) not in (required, required | {"structures"}):
        raise ValueError("模拟状态字段不完整或包含未知字段")
    if state["team"] not in ("red", "blue") or state["phase"] not in PHASES:
        raise ValueError("阵营或比赛阶段无效")
    if type(state["paused"]) is not bool:
        raise ValueError("paused 必须为布尔值")
    for key, limit in (("self_index", 5), ("remaining_seconds", 86400),
                       ("restart_count", 1000), ("allowed_shots", 1000000),
                       ("barrel_heat", 1000000)):
        number(state[key], 0, limit, True)
    for key, low, high in (("pitch_deg", -45, 45), ("yaw_deg", -180, 180),
                           ("speed_mps", 0, 100), ("chassis_power_w", 0, 1000000),
                           ("buffer_energy_j", 0, 1000000)):
        number(state[key], low, high)
    if not isinstance(state["coins"], dict) or set(state["coins"]) != {"red", "blue"}:
        raise ValueError("双方金币字段无效")
    if not isinstance(state["robots"], dict) or set(state["robots"]) != {"red", "blue"}:
        raise ValueError("双方机器人字段无效")
    for team in ("red", "blue"):
        number(state["coins"][team], 0, 1000000, True)
        robots = state["robots"][team]
        if not isinstance(robots, list) or len(robots) != 6:
            raise ValueError("每方必须为六个 HUD 位置；不是官方机器人编制")
        for robot in robots:
            if not isinstance(robot, dict) or set(robot) != {
                    "hp", "max_hp", "shield", "max_shield", "level", "ammo", "position"}:
                raise ValueError("机器人状态字段无效")
            number(robot["max_hp"], 1, 1000000, True)
            number(robot["hp"], 0, robot["max_hp"], True)
            number(robot["max_shield"], 1, 1000, True)
            number(robot["shield"], 0, robot["max_shield"], True)
            number(robot["level"], 1, 10, True)
            number(robot["ammo"], 0, 1000000, True)
            point = robot["position"]
            if not isinstance(point, list) or len(point) != 2:
                raise ValueError("地图位置必须为 [x, y]")
            for axis in point:
                number(axis, 0, 1)
    if (not isinstance(state["actions"], dict)
            or set(state["actions"]) != {"climb", "fire", "speed_up"}
            or any(type(v) is not bool for v in state["actions"].values())):
        raise ValueError("动作提示必须为三个布尔值")
    if "structures" in state:
        structures = state["structures"]
        if not isinstance(structures, dict) or set(structures) != {"red", "blue"}:
            raise ValueError("建筑状态必须包含双方")
        for team in ("red", "blue"):
            if not isinstance(structures[team], dict) or set(structures[team]) != {"base", "outpost"}:
                raise ValueError("建筑状态必须包含基地和前哨")
            for item in structures[team].values():
                if not isinstance(item, dict) or set(item) != {"hp", "max_hp"}:
                    raise ValueError("建筑血量字段无效")
                number(item["max_hp"], 1, 1000000, True)
                number(item["hp"], 0, item["max_hp"], True)
    return copy.deepcopy(state)


def state_packet(state, sequence, timestamp, event=None):
    """Derive all views from one state; death is hp == 0, not a second flag."""
    own = state["team"]
    enemy = "red" if own == "blue" else "blue"
    robot = state["robots"][own][state["self_index"]]
    payload = dict(channel="rm27.sim.v1.referee", sequence=sequence,
                   sent_at_unix_ms=timestamp, phase=state["phase"],
                   # Control-console metadata only; the current C++ HUD ignores these.
                   remaining_seconds=state["remaining_seconds"], paused=state["paused"],
                   telemetry=dict(hp=robot["hp"], max_hp=robot["max_hp"],
                                  ammo=robot["ammo"], chassis_power_w=state["chassis_power_w"],
                                  buffer_energy_j=state["buffer_energy_j"],
                                  barrel_heat=state["barrel_heat"], latency_ms=0,
                                  packet_loss_pct=0.0),
                   hud=dict(own_team=own, self_index=state["self_index"],
                            friendly_hp=[r["hp"] for r in state["robots"][own]],
                            enemy_hp=[r["hp"] for r in state["robots"][enemy]],
                            friendly_positions=[r["position"] for r in state["robots"][own]],
                            enemy_positions=[r["position"] for r in state["robots"][enemy]],
                            friendly_coins=state["coins"][own], enemy_coins=state["coins"][enemy],
                            level=robot["level"], shield=robot["shield"],
                            max_shield=robot["max_shield"],
                            **{key: state[key] for key in ("restart_count", "pitch_deg",
                               "yaw_deg", "speed_mps", "allowed_shots", "actions")}))
    if "structures" in state:
        payload["hud"]["structures"] = {
            f"{side}_{kind}": state["structures"][team][kind]
            for side, team in (("friendly", own), ("enemy", enemy))
            for kind in ("base", "outpost")}
    if event is not None:
        payload["event"] = event
    result = json.dumps(payload, ensure_ascii=True, allow_nan=False,
                        separators=(",", ":")).encode("ascii")
    if len(result) > 4096:
        raise ValueError("模拟消息超过客户端 4096 字节上限")
    return result


def validate_event(event):
    if not isinstance(event, dict) or set(event) != {"id", "category", "text"}:
        raise ValueError("事件字段无效")
    identifier, text = event["id"], event["text"]
    if (not isinstance(identifier, str) or not 1 <= len(identifier) <= 48
            or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"
                   for c in identifier)):
        raise ValueError("事件 ID 必须为不超过 48 字符的 ASCII 字母、数字、- 或 _")
    if (event["category"] not in ("system", "robot") or not isinstance(text, str)
            or not 1 <= len(text.encode("utf-16-le")) // 2 <= 120
            or not all(c.isprintable() for c in text)):
        raise ValueError("事件须为系统/机器人类别，正文为 1–120 字符单行文本")
    return copy.deepcopy(event)


def read_json(path: Path, limit=1024 * 1024):
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError("文件超过大小上限")
    try:
        return json.loads(data)
    except (ValueError, UnicodeError) as error:
        raise ValueError("不是有效 JSON 文件") from error
