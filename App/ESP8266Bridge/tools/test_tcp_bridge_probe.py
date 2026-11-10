import socket
import threading
import unittest

from tcp_bridge_probe import SAFE_BAD_START_FRAME, run_probe


class ProbeServer:
    def __init__(self, rounds):
        self.rounds = rounds
        self.frames = []
        self.error = None
        self.ready = threading.Event()
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.bind(("127.0.0.1", 0))
        self.host, self.port = self.server.getsockname()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.server.listen(1)
        self.thread.start()
        self.ready.wait(timeout=1.0)

    def join(self):
        self.thread.join(timeout=2.0)
        self.server.close()
        if self.error is not None:
            raise self.error

    def _run(self):
        self.ready.set()
        try:
            connection, _ = self.server.accept()
            with connection:
                for _ in range(self.rounds):
                    frame = bytearray()
                    while len(frame) < len(SAFE_BAD_START_FRAME):
                        chunk = connection.recv(len(SAFE_BAD_START_FRAME) - len(frame))
                        if not chunk:
                            raise RuntimeError("probe disconnected early")
                        frame.extend(chunk)
                    self.frames.append(bytes(frame))
                    connection.sendall(b"\xE1")
        except Exception as exc:
            self.error = exc


class TcpBridgeProbeTests(unittest.TestCase):
    def test_probe_sends_two_complete_frames_and_checks_responses(self):
        server = ProbeServer(rounds=2)
        server.start()

        statuses = run_probe(
            server.host,
            port=server.port,
            timeout=1.0,
            rounds=2,
            pause=0.0,
        )
        server.join()

        self.assertEqual(statuses, [0xE1, 0xE1])
        self.assertEqual(server.frames, [SAFE_BAD_START_FRAME] * 2)


if __name__ == "__main__":
    unittest.main()
