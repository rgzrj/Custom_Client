"""Loopback-only MQTT 3.1.1 broker for rm27.sim.v1.referee test data.

This is a project simulator, not the RoboMaster event-data protocol.
"""

import argparse
import socket
import time
import threading

try:
    from simulator.rm27_simulator import build_referee_packet
except ModuleNotFoundError:
    from rm27_simulator import build_referee_packet


ADDRESS = ("127.0.0.1", 39004)
TOPIC = b"rm27/sim/v1/referee"


def encode_packet(header: int, body: bytes) -> bytes:
    length = len(body)
    encoded = bytearray((header,))
    while True:
        digit = length % 128
        length //= 128
        encoded.append(digit | (0x80 if length else 0))
        if not length:
            return bytes(encoded) + body


def receive_packet(connection: socket.socket) -> tuple[int, bytes]:
    header = connection.recv(1)
    if not header:
        raise ConnectionError("client closed")
    length = 0
    multiplier = 1
    for _ in range(4):
        digit = connection.recv(1)
        if not digit:
            raise ConnectionError("truncated packet")
        length += (digit[0] & 0x7f) * multiplier
        if length > 4096:
            raise ValueError("packet too large")
        if not digit[0] & 0x80:
            break
        multiplier *= 128
    else:
        raise ValueError("invalid remaining length")
    body = bytearray()
    while len(body) < length:
        chunk = connection.recv(length - len(body))
        if not chunk:
            raise ConnectionError("truncated packet")
        body.extend(chunk)
    return header[0], bytes(body)


def subscribe_client(connection: socket.socket) -> None:
    connection.settimeout(3)
    header, body = receive_packet(connection)
    if header != 0x10 or not body.startswith(b"\x00\x04MQTT\x04\x02\x00\x00"):
        raise ValueError("expected MQTT 3.1.1 clean-session CONNECT")
    connection.sendall(b"\x20\x02\x00\x00")
    header, body = receive_packet(connection)
    if header != 0x82 or body[2:] != len(TOPIC).to_bytes(2, "big") + TOPIC + b"\x00":
        raise ValueError("expected fixed simulation-topic SUBSCRIBE")
    connection.sendall(encode_packet(0x90, body[:2] + b"\x00"))


def serve_client(connection: socket.socket, team: str, ticks: int) -> int:
    subscribe_client(connection)
    sequence = 0
    deadline = time.monotonic()
    while ticks == 0 or sequence < ticks:
        payload = build_referee_packet(sequence, int(time.time() * 1000), team)
        body = len(TOPIC).to_bytes(2, "big") + TOPIC + payload
        connection.sendall(encode_packet(0x30, body))
        sequence += 1
        deadline += 0.1
        time.sleep(max(0, deadline - time.monotonic()))
    return sequence


class ConsoleBroker:
    """One read-only local observer; no cached samples or robot command topics."""

    def __init__(self, log=lambda message: None, *, port=39004):
        if type(port) is not int or not 0 <= port <= 65535:
            raise ValueError("MQTT 端口必须为 0–65535 整数（0 仅供隔离测试）")
        self.log = log
        self.lock = threading.Lock()
        self.closed = threading.Event()
        self.enabled = True
        self.client = None
        self.pending = None
        self.server = socket.create_server(("127.0.0.1", port))
        self.server.settimeout(0.1)
        self.worker = threading.Thread(target=self._accept, daemon=True)
        self.worker.start()

    def _accept(self):
        while not self.closed.is_set():
            try:
                connection = self.server.accept()[0]
            except socket.timeout:
                continue
            except OSError:
                break
            with self.lock:
                if not self.enabled or self.closed.is_set() or self.client is not None:
                    connection.close()
                    continue
                self.pending = connection
            try:
                subscribe_client(connection)
                connection.settimeout(0.05)
                with self.lock:
                    if not self.enabled or self.closed.is_set() or self.pending is not connection:
                        connection.close()
                    else:
                        self.client = connection
                        self.log("MQTT 客户端已订阅；只发送当前新数据")
                    self.pending = None
            except (OSError, ValueError, ConnectionError):
                connection.close()
                with self.lock:
                    if self.pending is connection:
                        self.pending = None

    def set_enabled(self, enabled):
        with self.lock:
            self.enabled = enabled
            if not enabled:
                self._disconnect()

    def _disconnect(self):
        for connection in (self.client, self.pending):
            if connection:
                try:
                    connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                connection.close()
        self.client = self.pending = None

    def publish(self, payload):
        with self.lock:
            if not self.enabled or self.client is None:
                return False
            try:
                body = len(TOPIC).to_bytes(2, "big") + TOPIC + payload
                self.client.sendall(encode_packet(0x30, body))
                return True
            except OSError:
                self._disconnect()
                self.log("MQTT 连接断开，等待重新订阅")
                return False

    def close(self):
        self.closed.set()
        self.server.close()
        with self.lock:
            self._disconnect()
        self.worker.join(timeout=1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--team", choices=("red", "blue"), default="blue")
    parser.add_argument("--ticks", type=int, default=0, help="0 runs until interrupted")
    args = parser.parse_args()
    if args.ticks < 0:
        parser.error("--ticks must be non-negative")
    print(f"Project MQTT simulation listening on {ADDRESS[0]}:{ADDRESS[1]}")
    with socket.create_server(ADDRESS) as server:
        try:
            while True:
                with server.accept()[0] as connection:
                    try:
                        sent = serve_client(connection, args.team, args.ticks)
                        print(f"Sent {sent} project-simulation samples")
                        if args.ticks:
                            break
                    except (ConnectionError, OSError, ValueError) as error:
                        print(f"Simulation client disconnected: {error}")
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
