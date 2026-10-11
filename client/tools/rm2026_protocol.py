"""Offline RM2026 V2.0.0 payload inspector. No sockets or command publisher."""

import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
import time

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
from google.protobuf.message import DecodeError

VERSION = "rm2026-v2.0.0"
INBOUND_TOPICS = frozenset((
    "GameStatus", "GlobalUnitStatus", "GlobalLogisticsStatus", "Event",
    "RobotRespawnStatus", "RobotStaticStatus", "RobotDynamicStatus",
    "RobotModuleStatus", "RobotPosition", "PenaltyInfo", "CustomByteBlock",
    "Buff", "RobotPerformanceSelectionSync", "DeployModeStatusSync",
))
MAX_PAYLOAD = 65536  # Project resource bound, not an official protocol limit.


class Protocol:
    def __init__(self, descriptor_path):
        descriptors = descriptor_pb2.FileDescriptorSet()
        descriptors.ParseFromString(Path(descriptor_path).read_bytes())
        self.pool = descriptor_pool.DescriptorPool()
        for descriptor in descriptors.file:
            self.pool.Add(descriptor)

    def message(self, name):
        descriptor = self.pool.FindMessageTypeByName("rm2026.v2." + name)
        return message_factory.GetMessageClass(descriptor)()

    def decode(self, version, topic, payload):
        if version != VERSION:
            raise ValueError("protocol version mismatch; no automatic 2027 fallback")
        if topic not in INBOUND_TOPICS:
            raise ValueError("unsupported inbound topic: " + topic)
        if len(payload) > MAX_PAYLOAD:
            raise ValueError("payload exceeds project memory limit")
        message = self.message(topic)
        try:
            message.ParseFromString(payload)
        except DecodeError as error:
            raise ValueError("malformed Protobuf payload") from error
        fields = {}
        for field, value in message.ListFields():
            if field.label == field.LABEL_REPEATED:
                value = list(value)
            if isinstance(value, float) and not math.isfinite(value):
                raise ValueError("non-finite value: " + field.name)
            fields[field.name] = value
        if topic == "GlobalUnitStatus" and fields.get("robot_health"):
            if len(fields["robot_health"]) != 10:
                raise ValueError("2026 robot_health must have ten values")
        if topic == "CustomByteBlock" and len(fields.get("data", b"")) > 300:
            raise ValueError("CustomByteBlock exceeds 2.4 kbit")
        # Optional absence stays absent; an empty/default message is not evidence
        # of fresh referee data. Unknown fields are accepted by Protobuf.
        return fields


class Snapshot:
    """Per-topic receive freshness, separate from connection and GUI state."""

    def __init__(self, protocol, clock=time.monotonic_ns):
        self.protocol = protocol
        self.clock = clock
        self.samples = {}

    def accept(self, version, topic, payload, *, retained=False):
        if retained:
            return False  # Historical retained delivery cannot prove live data.
        fields = self.protocol.decode(version, topic, payload)
        if not fields:
            return False
        self.samples[topic] = (self.clock(), fields)
        return True

    def fresh(self, topic, maximum_age_ms=1500):
        if maximum_age_ms <= 0:
            raise ValueError("freshness window must be positive")
        sample = self.samples.get(topic)
        if sample is None:
            return None
        received, fields = sample
        if self.clock() - received >= maximum_age_ms * 1000000:
            return None
        return deepcopy(fields)

    def reset(self):
        self.samples.clear()  # Explicit session/disconnect reset; no old replay.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--descriptor", required=True, type=Path)
    parser.add_argument("--protocol", required=True, choices=[VERSION])
    parser.add_argument("--topic", required=True, choices=sorted(INBOUND_TOPICS))
    parser.add_argument("--payload", required=True, type=Path)
    args = parser.parse_args()
    try:
        if args.payload.stat().st_size > MAX_PAYLOAD:
            parser.error("payload exceeds project memory limit")
        with args.payload.open("rb") as stream:
            payload = stream.read(MAX_PAYLOAD + 1)
        fields = Protocol(args.descriptor).decode(args.protocol, args.topic, payload)
    except (ValueError, OSError, KeyError, DecodeError) as error:
        parser.error(str(error))
    print(json.dumps({"protocol": VERSION, "topic": args.topic, "fields": fields},
                     ensure_ascii=False, indent=2,
                     default=lambda value: {"hex": value.hex()}))


if __name__ == "__main__":
    main()
