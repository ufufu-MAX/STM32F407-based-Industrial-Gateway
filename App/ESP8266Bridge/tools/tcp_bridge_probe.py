"""Safe TCP-to-UART bridge probe for the STM32 OTA link."""

import argparse
import socket
import sys
import time


DEFAULT_PORT = 8266
DEFAULT_TIMEOUT = 3.0
DEFAULT_ROUNDS = 2
EXPECTED_STATUS = 0xE1

# Deliberately wrong first header byte. STM32 should reject this 18-byte
# start frame before entering an OTA session and return one-byte status 0xE1.
SAFE_BAD_START_FRAME = bytes.fromhex(
    "A4 A5 01 00 01 00 01 00 00 00 00 04 00 00 00 00 5A 5A"
)


def receive_one(sock, timeout):
    sock.settimeout(timeout)
    response = sock.recv(1)
    if not response:
        raise RuntimeError("bridge closed the TCP connection")
    return response[0]


def run_probe(host, port=DEFAULT_PORT, timeout=DEFAULT_TIMEOUT,
              rounds=DEFAULT_ROUNDS, pause=0.1):
    results = []
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        for round_index in range(rounds):
            sock.sendall(SAFE_BAD_START_FRAME)
            status = receive_one(sock, timeout)
            results.append(status)
            if status != EXPECTED_STATUS:
                raise RuntimeError(
                    f"round {round_index + 1}: expected 0xE1, got 0x{status:02X}"
                )
            if round_index + 1 < rounds:
                time.sleep(pause)
    return results


def main():
    parser = argparse.ArgumentParser(
        description="Probe the ESP8266 TCP-UART bridge without starting OTA"
    )
    parser.add_argument("--host", required=True, help="ESP8266 LAN IP address")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--rounds", type=int, default=DEFAULT_ROUNDS)
    args = parser.parse_args()

    if not 1 <= args.port <= 65535:
        parser.error("port must be in 1..65535")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    if args.rounds <= 0:
        parser.error("rounds must be positive")

    try:
        statuses = run_probe(
            args.host,
            port=args.port,
            timeout=args.timeout,
            rounds=args.rounds,
        )
    except (OSError, RuntimeError) as exc:
        print(f"FAIL: {exc}")
        return 1

    for index, status in enumerate(statuses, start=1):
        print(f"round {index}: response 0x{status:02X}")
    print("PASS: TCP-UART round trip is stable; OTA was not started")
    return 0


if __name__ == "__main__":
    sys.exit(main())
