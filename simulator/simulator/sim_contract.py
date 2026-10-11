"""rm27.sim.v1.referee 的本项目线协议校验；不是官方协议。"""

import json
import math

from simulator.sim_state import PHASES, validate_event

SAFE_INTEGER = 9007199254740991


def numeric(value, low, high, integer=False):
    if (type(value) not in (int, float) or not low <= value <= high
            or not math.isfinite(value) or (integer and value != math.floor(value))):
        raise ValueError("模拟线协议数值越界或类型错误")


def validate_packet(payload):
    if not payload or len(payload) > 4096:
        raise ValueError("模拟载荷必须为 1–4096 字节")
    packet = json.loads(payload)
    if not isinstance(packet, dict) or packet.get("channel") != "rm27.sim.v1.referee":
        raise ValueError("不是本项目 v1 裁判载荷")
    numeric(packet.get("sequence"), 0, SAFE_INTEGER, True)
    numeric(packet.get("sent_at_unix_ms"), 1, SAFE_INTEGER, True)
    if packet.get("phase") not in PHASES:
        raise ValueError("模拟阶段无效")
    telemetry = packet.get("telemetry")
    if not isinstance(telemetry, dict):
        raise ValueError("telemetry 必须为对象")
    for key in ("hp", "max_hp", "barrel_heat", "ammo", "latency_ms"):
        numeric(telemetry.get(key), 1 if key == "max_hp" else 0,
                60000 if key == "latency_ms" else 1000000, True)
    for key in ("chassis_power_w", "buffer_energy_j", "packet_loss_pct"):
        numeric(telemetry.get(key), 0, 100 if key == "packet_loss_pct" else 1000000)
    if telemetry["hp"] > telemetry["max_hp"]:
        raise ValueError("血量不能超过上限")
    if "hud" in packet:
        hud = packet["hud"]
        if not isinstance(hud, dict) or hud.get("own_team") not in ("red", "blue"):
            raise ValueError("HUD 阵营无效")
        for key, low, high in (("friendly_coins", 0, 1000000), ("enemy_coins", 0, 1000000),
                               ("restart_count", 0, 1000), ("allowed_shots", 0, 1000000)):
            numeric(hud.get(key), low, high, True)
        numeric(hud.get("pitch_deg"), -45, 45)
        numeric(hud.get("speed_mps"), 0, 100)
        for key, low, high, integral in (("yaw_deg", -180, 180, False),
                                        ("self_index", 0, 5, True), ("level", 1, 10, True)):
            if key in hud:
                numeric(hud[key], low, high, integral)
        if ("shield" in hud) != ("max_shield" in hud):
            raise ValueError("护盾字段必须成对")
        if "shield" in hud:
            numeric(hud["shield"], 0, 1000, True)
            numeric(hud["max_shield"], 1, 1000, True)
            if hud["shield"] > hud["max_shield"]:
                raise ValueError("护盾超过上限")
        if "actions" in hud:
            if not isinstance(hud["actions"], dict) or any(
                    type(hud["actions"].get(key)) is not bool for key in ("climb", "fire", "speed_up")):
                raise ValueError("动作提示必须为布尔值")
        if "structures" in hud:
            structures = hud["structures"]
            if not isinstance(structures, dict):
                raise ValueError("structures 必须为对象")
            for key in ("friendly_base", "enemy_base", "friendly_outpost", "enemy_outpost"):
                item = structures.get(key)
                if not isinstance(item, dict):
                    raise ValueError("建筑血量必须包含双方基地与前哨")
                numeric(item.get("hp"), 0, 1000000, True)
                numeric(item.get("max_hp"), 1, 1000000, True)
                if item["hp"] > item["max_hp"]:
                    raise ValueError("建筑血量不能超过上限")
        for key in ("friendly_hp", "enemy_hp", "friendly_positions", "enemy_positions"):
            values = hud.get(key)
            if not isinstance(values, list) or len(values) != 6:
                raise ValueError("HUD 队列必须为六个位置")
            for value in values:
                if key.endswith("_hp"):
                    numeric(value, 0, 1000000, True)
                else:
                    if not isinstance(value, list) or len(value) != 2:
                        raise ValueError("坐标必须为二维数组")
                    for axis in value:
                        numeric(axis, 0, 1)
    if "event" in packet:
        event = packet["event"]
        if not isinstance(event, dict):
            raise ValueError("event 必须为对象")
        # v1 接收端忽略扩展字段；已有三个字段仍严格校验。
        validate_event({key: event.get(key) for key in ("id", "category", "text")})
    return packet
