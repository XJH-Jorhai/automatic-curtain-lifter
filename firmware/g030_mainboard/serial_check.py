"""Validate fresh UART TX and RX; does not reset the board or send motion commands."""
import argparse
import secrets
import time
import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    token = secrets.token_hex(8)
    expected = f"PONG {token}".encode()
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with port:
        # Wireless UART settings/forwarding may settle after the CDC port opens.
        time.sleep(0.5)
        port.reset_input_buffer()
        port.write(f"PING {token}\r\nMOTOR STATUS\r\n".encode())
        port.flush()
        deadline = time.monotonic() + 8
        received = bytearray()
        while time.monotonic() < deadline:
            received.extend(port.read(4096))
            if expected in received and b"MOTOR phase=0 " in received and b"EN=0 SLEEP=0" in received:
                print(received.decode("ascii", errors="replace"))
                print(f"PASS: fresh PONG {token} and requested idle status at 115200 8N1")
                return
        print(received.decode("ascii", errors="replace"))
        raise SystemExit("FAIL: requested idle status and fresh PONG were not both received")


if __name__ == "__main__":
    main()
