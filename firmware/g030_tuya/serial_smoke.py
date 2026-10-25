"""Hardware UART smoke test for the G030 bench bridge (DAPLink serial on PA9/PA10).

Run before moving DAPLink's serial pins to PA2/PA3 and connecting T1-3S.
Requires pyserial. The script sends only protocol requests; it cannot move a motor.
"""

import argparse
import json
import time

import serial


def frame(command: int, payload: bytes = b"") -> bytes:
    header = bytes((0x55, 0xAA, 0x00, command, len(payload) >> 8,
                    len(payload) & 0xFF))
    body = header + payload
    return body + bytes((sum(body) & 0xFF,))


def read_frame(port: serial.Serial, deadline: float) -> tuple[int, bytes]:
    while time.monotonic() < deadline:
        if port.read(1) != b"\x55":
            continue
        if port.read(1) != b"\xaa":
            continue
        header = port.read(4)
        if len(header) != 4:
            continue
        version, command, high, low = header
        length = (high << 8) | low
        if version != 3 or length > 128:
            continue
        tail = port.read(length + 1)
        if len(tail) != length + 1:
            continue
        whole = b"\x55\xaa" + header + tail
        if (sum(whole[:-1]) & 0xFF) != whole[-1]:
            continue
        return command, tail[:-1]
    raise TimeoutError("no valid MCU response before timeout")


def request(port: serial.Serial, command: int, payload: bytes = b"",
            replies: int = 1) -> list[bytes]:
    port.reset_input_buffer()
    port.write(frame(command, payload))
    deadline = time.monotonic() + 2.5
    received = []
    for _ in range(replies):
        reply_command, reply_payload = read_frame(port, deadline)
        expected = 7 if command in (6, 8) else command
        if reply_command != expected:
            raise AssertionError(f"request {command:02x}: unexpected reply {reply_command:02x}")
        received.append(reply_payload)
    return received


def dp_payload(dpid: int, kind: int, value: bytes) -> bytes:
    return bytes((dpid, kind, 0, len(value))) + value


def dp_value(payload: bytes) -> tuple[int, int]:
    if len(payload) < 5 or len(payload) != payload[3] + 4:
        raise AssertionError(f"invalid DP report: {payload.hex()}")
    return payload[0], int.from_bytes(payload[4:], "big")


def run(port_name: str) -> None:
    with serial.Serial(port_name, 9600, timeout=0.15, write_timeout=1) as port:
        time.sleep(0.15)
        heartbeat = request(port, 0)[0]
        assert heartbeat in (b"\x00", b"\x01"), heartbeat
        product = json.loads(request(port, 1)[0].decode("ascii"))
        assert product["p"] == "YOUR_PRODUCT_ID", product
        assert request(port, 2)[0] == b""
        initial = dict(map(dp_value, request(port, 8, replies=3)))
        assert initial == {1: 1, 2: 0, 101: 0}, initial

        target_reports = dict(map(dp_value, request(
            port, 6, dp_payload(2, 2, (37).to_bytes(4, "big")), replies=2)))
        assert target_reports == {2: 37, 101: 0}, target_reports
        current = dict(map(dp_value, request(port, 8, replies=3)))
        assert current == {1: 1, 2: 37, 101: 0}, current

        rejected = dp_value(request(port, 6, dp_payload(1, 4, b"\x00"))[0])
        assert rejected == (1, 1), rejected
        port.write(frame(6, dp_payload(99, 4, b"\x00")))
        time.sleep(0.05)
        port.write(frame(6, bytes((2, 2, 1, 0, 0, 0, 0, 50))))
        time.sleep(0.05)
        assert request(port, 0)[0] == b"\x01"
        print("PASS: heartbeat, product, work mode, state query, DP2 target, "
              "motor command rejection, malformed/unknown DP recovery")
        print(f"MCU product={product['p']}, target={current[2]}, actual={current[101]}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    run(args.port)
