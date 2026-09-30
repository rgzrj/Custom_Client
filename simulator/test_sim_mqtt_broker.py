import socket
import threading
import unittest

from simulator.sim_mqtt_broker import TOPIC, encode_packet, receive_packet, serve_client


class SimMqttBrokerTests(unittest.TestCase):
    def test_packet_lengths(self):
        for size in (0, 127, 128, 4096):
            left, right = socket.socketpair()
            with left, right:
                left.sendall(encode_packet(0x30, b"x" * size))
                self.assertEqual(receive_packet(right), (0x30, b"x" * size))

    def test_loopback_subscription(self):
        server, client = socket.socketpair()
        failures = []

        def run():
            try:
                with server:
                    self.assertEqual(serve_client(server, "blue", 2), 2)
            except Exception as error:
                failures.append(error)

        worker = threading.Thread(target=run)
        worker.start()
        with client:
            client.settimeout(3)
            client.sendall(encode_packet(0x10, b"\x00\x04MQTT\x04\x02\x00\x00\x00\x04test"))
            self.assertEqual(receive_packet(client), (0x20, b"\x00\x00"))
            subscription = b"\x00\x01" + len(TOPIC).to_bytes(2, "big") + TOPIC + b"\x00"
            client.sendall(encode_packet(0x82, subscription))
            self.assertEqual(receive_packet(client), (0x90, b"\x00\x01\x00"))
            for sequence in range(2):
                header, body = receive_packet(client)
                self.assertEqual(header, 0x30)
                self.assertTrue(body.startswith(len(TOPIC).to_bytes(2, "big") + TOPIC))
                self.assertIn(f'"sequence":{sequence}'.encode(), body)
        worker.join(timeout=3)
        self.assertFalse(worker.is_alive())
        self.assertEqual(failures, [])


if __name__ == "__main__":
    unittest.main()
