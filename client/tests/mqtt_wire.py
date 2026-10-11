"""Small stdlib MQTT wire helpers for scripted loopback test peers only."""

import struct


def packet(header, body):
    length = len(body)
    encoded = bytearray([header])
    while True:
        digit = length % 128
        length //= 128
        encoded.append(digit | (0x80 if length else 0))
        if not length:
            return bytes(encoded) + body


def read_exact(stream, length):
    result = b""
    while len(result) < length:
        chunk = stream.recv(length - len(result))
        if not chunk:
            raise EOFError("peer closed")
        result += chunk
    return result


def read_packet(stream, max_body=4096):
    header = read_exact(stream, 1)[0]
    length = 0
    for shift in (0, 7, 14, 21):
        digit = read_exact(stream, 1)[0]
        length |= (digit & 0x7f) << shift
        if not digit & 0x80:
            if length > max_body:
                raise ValueError("MQTT test peer body exceeds limit")
            return header, read_exact(stream, length)
    raise AssertionError("invalid MQTT test packet")


def publish(stream, topic, payload, *, mid=None, duplicate=False, retained=False):
    topic = topic.encode("utf-8")
    body = struct.pack("!H", len(topic)) + topic
    if mid is not None:
        body += struct.pack("!H", mid)
    stream.sendall(packet(0x30 | (2 if mid is not None else 0) |
                          (8 if duplicate else 0) | int(retained), body + payload))
