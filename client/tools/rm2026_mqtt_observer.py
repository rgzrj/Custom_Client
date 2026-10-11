"""Local-only, read-only RM2026 MQTT developer probe; not a Qt/HUD adapter."""

import argparse
from collections import OrderedDict
import hashlib
import json
from pathlib import Path
import threading
import time
import uuid

import paho.mqtt.client as mqtt
from google.protobuf.message import DecodeError

from rm2026_protocol import INBOUND_TOPICS, MAX_PAYLOAD, Protocol, Snapshot, VERSION


class Observer:
    def __init__(self, protocol, version, *, host="127.0.0.1", port=39005,
                 clock=time.monotonic_ns):
        if version != VERSION:
            raise ValueError("explicit rm2026-v2.0.0 selection required")
        if host != "127.0.0.1" or not 1 <= port <= 65535:
            raise ValueError("only literal 127.0.0.1 and ports 1..65535 are allowed")
        for topic in INBOUND_TOPICS:
            protocol.message(topic)  # Fail on an incomplete descriptor before connecting.
        self._port = port
        self._lock = threading.RLock()
        self._snapshot = Snapshot(protocol, clock)
        self._duplicates = OrderedDict()
        self._link = "stopped"
        self._subscription = None
        self._sessions = 0
        self._counts = dict(accepted=0, rejected=0, retained=0, duplicates=0)
        self._client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id="rm2026-local-observer-" + uuid.uuid4().hex,
            clean_session=True, protocol=mqtt.MQTTv311)
        self._client.reconnect_delay_set(min_delay=1, max_delay=8)
        self._client.on_connect = self._on_connect
        self._client.on_connect_fail = self._on_connect_fail
        self._client.on_disconnect = self._on_disconnect
        self._client.on_subscribe = self._on_subscribe
        self._client.on_message = self._on_message

    def _reset(self, link):
        self._link = link
        self._subscription = None
        self._snapshot.reset()
        self._duplicates.clear()

    def start(self):
        with self._lock:
            if self._link != "stopped":
                raise ValueError("observer already started")
            self._reset("connecting")
        self._client.connect_async("127.0.0.1", self._port, keepalive=10)
        self._client.loop_start()  # Network callbacks never block on terminal output.

    def stop(self):
        self._client.disconnect()
        self._client.loop_stop()
        with self._lock:
            self._reset("stopped")

    def _on_connect(self, client, userdata, flags, reason, properties):
        with self._lock:
            self._reset("connection-rejected" if reason.is_failure else "awaiting-suback")
            if reason.is_failure:
                return
            self._sessions += 1
            result, mid = client.subscribe([(topic, 1) for topic in sorted(INBOUND_TOPICS)])
            if result == mqtt.MQTT_ERR_SUCCESS:
                self._subscription = mid
            else:
                self._link = "subscription-rejected"

    def _on_connect_fail(self, client, userdata):
        with self._lock:
            self._reset("offline")

    def _on_disconnect(self, client, userdata, flags, reason, properties):
        with self._lock:
            self._reset("offline")

    def _on_subscribe(self, client, userdata, mid, reasons, properties):
        with self._lock:
            if mid != self._subscription:
                return
            approved = len(reasons) == len(INBOUND_TOPICS) and all(
                reason.value in (0, 1) for reason in reasons)
            self._link = "subscribed" if approved else "subscription-rejected"
            if not approved:
                self._snapshot.reset()

    def _on_message(self, client, userdata, message):
        with self._lock:
            if self._link != "subscribed" or message.qos not in (0, 1):
                self._counts["rejected"] += 1
                return
            if message.retain:
                self._counts["retained"] += 1
                return
            # ponytail: Paho has already buffered the MQTT packet here. This is
            # a local probe, not a hostile-broker memory guard; native ingress
            # must limit Remaining Length before allocating a production packet.
            if len(message.payload) > MAX_PAYLOAD:
                self._counts["rejected"] += 1
                return
            try:
                topic = message.topic
            except UnicodeDecodeError:
                self._counts["rejected"] += 1
                return
            if message.qos == 1:
                signature = (topic, hashlib.sha256(message.payload).digest())
                if message.dup:
                    known = self._duplicates.get(message.mid) == signature
                    self._counts["duplicates" if known else "rejected"] += 1
                    # An uncached DUP may be a first delivery or an old retry.
                    # This read-only probe fails closed rather than making it fresh.
                    return
                self._duplicates[message.mid] = signature
                self._duplicates.move_to_end(message.mid)
                if len(self._duplicates) > 256:  # Project bound, per connection.
                    self._duplicates.popitem(last=False)
            try:
                accepted = self._snapshot.accept(VERSION, topic, message.payload)
            except ValueError:
                accepted = False
            self._counts["accepted" if accepted else "rejected"] += 1
            # Paho sends PUBACK after this callback, even for rejected payloads:
            # acknowledgement means receipt, not successful Protobuf validation.

    def status(self, maximum_age_ms=1500):
        with self._lock:
            if maximum_age_ms <= 0:
                raise ValueError("freshness window must be positive")
            fresh = {}
            stale = []
            for topic in sorted(self._snapshot.samples):
                fields = self._snapshot.fresh(topic, maximum_age_ms)
                if fields is None:
                    stale.append(topic)
                else:
                    fresh[topic] = fields
            return dict(protocol=VERSION, local_only=True, link=self._link,
                        sessions=self._sessions, counts=dict(self._counts),
                        fresh=fresh, stale=stale)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--descriptor", required=True, type=Path)
    parser.add_argument("--protocol", required=True, choices=[VERSION])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=39005, type=int)
    args = parser.parse_args()
    try:
        observer = Observer(Protocol(args.descriptor), args.protocol,
                            host=args.host, port=args.port)
    except (ValueError, OSError, KeyError, DecodeError) as error:
        parser.error(str(error))
    try:
        observer.start()
        previous = None
        while True:
            report = json.dumps(observer.status(), ensure_ascii=False,
                                default=lambda value: {"hex": value.hex()})
            if report != previous:
                print(report, flush=True)
                previous = report
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    finally:
        observer.stop()


if __name__ == "__main__":
    main()
