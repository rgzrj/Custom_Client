"""Offline M0 audit of a pinned RM26_Client checkout; no network or sockets."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory

COMMIT = "e291f2f65f5a5924509e4e2fe1b148ff9d4b24af"
SOURCE_SHA256 = {
    "LICENSE": "a0bf61b72dd3966daf921906256b5f7b1c79de9181c27eab3dfaa151541eb5a4",
    "src/network/proto/robomaster.proto": "275ff79249557cf3c46defc0f28baeb1661bf9e654083022279e3fe5c98fd7a2",
    "src/network/MqttManager.cpp": "3deefa009f755553b7f0ae1a479c6ebc44138b55736e5f773b27a21976a5fa52",
    "src/network/NetworkManager.cpp": "6659c33b5d1c19affd4d69ca28b5271a5d2ab02af44eb6c19ecd23f786320e6b",
    "src/core/GameData.cpp": "c2d620323053f6589fc6c6505676d3f2b85ee03a8453b28e50976e7cc39e4d04",
}
# Order and limits: official V2.0.0, section 2.1 (PDF pages 45-49).
TOPICS = (
    ("KeyboardMouseControl", "up", "75Hz"),
    ("CustomControl", "up", "75Hz; <=30 bytes"),
    ("GameStatus", "down", "5Hz"),
    ("GlobalUnitStatus", "down", "1Hz"),
    ("GlobalLogisticsStatus", "down", "1Hz"),
    ("GlobalSpecialMechanism", "down", "1Hz"),
    ("Event", "down", "trigger"),
    ("RobotInjuryStat", "down", "1Hz"),
    ("RobotRespawnStatus", "down", "1Hz"),
    ("RobotStaticStatus", "down", "1Hz"),
    ("RobotDynamicStatus", "down", "10Hz"),
    ("RobotModuleStatus", "down", "1Hz"),
    ("RobotPosition", "down", "1Hz"),
    ("Buff", "down", "trigger, then 1Hz until lost"),
    ("PenaltyInfo", "down", "trigger"),
    ("RobotPathPlanInfo", "down", "trigger"),
    ("MapClickInfo", "down", "trigger; section 1.3.1"),
    ("MapClickCmd", "up", "section 1.3.1: >=0.5s gunner; >=3s semi-auto operator"),
    ("RadarInfoToClient", "down", "trigger"),
    ("CustomByteBlock", "down", "50Hz; <=300 bytes"),
    ("AssemblyCommand", "up", "<=10Hz"),
    ("TechCoreMotionStateSync", "down", "1Hz"),
    ("RobotPerformanceSelectionCommand", "up", "<=10Hz"),
    ("RobotPerformanceSelectionSync", "down", "1Hz"),
    ("CommonCommand", "up", "<=10Hz"),
    ("HeroDeployModeEventCommand", "up", "<=10Hz"),
    ("DeployModeStatusSync", "down", "1Hz"),
    ("RuneActivateCommand", "up", "<=10Hz"),
    ("RuneStatusSync", "down", "1Hz"),
    ("SentryStatusSync", "down", "1Hz"),
    ("DartCommand", "up", "<=10Hz"),
    ("DartSelectTargetStatusSync", "down", "1Hz"),
    ("SentryCtrlCommand", "up", "<=10Hz"),
    ("SentryCtrlResult", "down", "trigger"),
    ("AirSupportCommand", "up", "trigger; no numeric ceiling in section 2.1"),
    ("AirSupportStatusSync", "down", "1Hz"),
)


def compile_schema(protoc, source, output):
    subprocess.run([str(protoc), "--proto_path=" + str(source.parent),
                    "--descriptor_set_out=" + str(output), str(source)], check=True)
    descriptor = descriptor_pb2.FileDescriptorSet()
    descriptor.ParseFromString(output.read_bytes())
    return descriptor


def fields(message):
    return [{"number": f.number, "name": f.name,
             "type": descriptor_pb2.FieldDescriptorProto.Type.Name(f.type)[5:].lower(),
             "presence": "repeated" if f.label == f.LABEL_REPEATED else
                         "explicit" if f.proto3_optional else "implicit"}
            for f in message.field]


def audit(protoc, upstream, project):
    for path, expected_hash in SOURCE_SHA256.items():
        if hashlib.sha256((upstream / path).read_bytes()).hexdigest() != expected_hash:
            raise ValueError("fixed upstream source differs: " + path)
    with tempfile.TemporaryDirectory(prefix="rm2026-audit-") as directory:
        directory = Path(directory)
        local = compile_schema(protoc, project / "contracts/rm2026-v2.0.0/client.proto",
                               directory / "local.desc")
        source = compile_schema(protoc, upstream / "src/network/proto/robomaster.proto",
                                directory / "upstream.desc")
        adopted = compile_schema(protoc, project / "third_party/rm26_client/src/network/proto/robomaster.proto",
                                 directory / "adopted.desc")
    local_messages = {m.name: m for f in local.file for m in f.message_type}
    source_messages = {m.name: m for f in source.file for m in f.message_type}
    adopted_messages = {m.name: m for f in adopted.file for m in f.message_type}
    expected = {name for name, _, _ in TOPICS} - {"RadarInfoToClient"}
    if set(local_messages) != expected:
        raise ValueError("canonical schema does not match the 35 verified definitions")
    for name, message in local_messages.items():
        if fields(message) != fields(adopted_messages[name]):
            raise ValueError(f"{name}: adopted upstream schema drifted from the canonical schema")
    # Exercise generated classes: explicit zero must survive the wire, absence must not.
    pool = descriptor_pool.DescriptorPool()
    for file in local.file:
        pool.Add(file)
    presence_checks = 0
    for name, message in local_messages.items():
        cls = message_factory.GetMessageClass(pool.FindMessageTypeByName("rm2026.v2." + name))
        for f in message.field:
            if f.label == f.LABEL_REPEATED:
                continue
            if not f.proto3_optional:
                raise ValueError(f"{name}.{f.name}: scalar presence is implicit")
            instance = cls()
            value = {f.TYPE_BYTES: b"", f.TYPE_STRING: "", f.TYPE_BOOL: False,
                     f.TYPE_FLOAT: 0.0}.get(f.type, 0)
            setattr(instance, f.name, value)
            decoded = cls.FromString(instance.SerializeToString())
            if not decoded.HasField(f.name) or cls().HasField(f.name):
                raise ValueError(f"{name}.{f.name}: zero/absence collapsed")
            presence_checks += 1
    network = (upstream / "src/network/NetworkManager.cpp").read_text(encoding="utf-8-sig")
    subscription_block = network.split("QStringList topics = {", 1)[1].split("};", 1)[0]
    subscriptions = re.findall(r'"([A-Za-z]+)"', subscription_block)
    observer = (project / "tools/rm2026_protocol.py").read_text(encoding="utf-8-sig")
    observer_block = observer.split("INBOUND_TOPICS = frozenset((", 1)[1].split("))", 1)[0]
    current = set(re.findall(r'"([A-Za-z]+)"', observer_block))
    rows = []
    for section, (name, direction, frequency) in enumerate(TOPICS, 1):
        ours = fields(local_messages[name]) if name in local_messages else None
        theirs = fields(source_messages[name]) if name in source_messages else None
        rows.append({"topic": name, "direction": direction, "highest_qos": 1,
                     "frequency": frequency, "official_section": f"2.2.{section}",
                     "canonical_fields": ours, "upstream_fields": theirs,
                     "upstream_subscribed": name in subscriptions,
                     "current_observer": name in current,
                     "differences": [f for f in ours if f not in (theirs or [])] if ours else [],
                     "status": "blocked: ambiguous official Radar outer field" if ours is None
                               else "schema verified; runtime acceptance pending"})
    return {"protocol": "rm2026-v2.0.0", "official_pdf":
            "RoboMaster 2026 机甲大师高校系列赛通信协议 V2.0.0（20260626）.pdf",
            "upstream_repository": "https://github.com/ClearWei/RM26_Client",
            "upstream_commit": COMMIT,
            "source_sha256": SOURCE_SHA256,
            "verified_messages": len(local_messages), "zero_presence_wire_checks": presence_checks,
            "upstream_subscription_count": len(subscriptions),
            "upstream_distinct_subscription_count": len(set(subscriptions)),
            "upstream_extra_subscriptions": sorted(set(subscriptions) -
                {n for n, d, _ in TOPICS if d == "down"}),
            "topics": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--protoc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.protoc.resolve(), args.upstream.resolve(), Path(__file__).resolve().parents[1])
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"35 verified schemas; {result['zero_presence_wire_checks']} zero/absence checks; "
          f"{result['upstream_distinct_subscription_count']} distinct upstream subscriptions")


if __name__ == "__main__":
    main()
