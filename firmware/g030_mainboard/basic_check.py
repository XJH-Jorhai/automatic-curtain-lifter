"""UART helper and non-motion v3 checks. EEPROM writes are unavailable."""
import argparse
import secrets
import time
import serial


class Board:
    def __init__(self, port):
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        time.sleep(0.5)
        self.serial.reset_input_buffer()

    def command(self, text, prefix, timeout=3):
        self.serial.write((text + "\r\n").encode("ascii"))
        self.serial.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.serial.readline().decode("ascii", errors="replace").strip()
            if not line or line.startswith("HB "):
                continue
            if line.startswith("ERR"):
                raise RuntimeError(f"{text}: {line}")
            if line.startswith(prefix):
                return line
            print("UNEXPECTED", repr(line), flush=True)
        raise TimeoutError(text)

    def read(self, address):
        line = self.command(f"EE R {address}", f"EE DATA {address} ")
        data = bytes.fromhex(line.split()[-1])
        if len(data) != 16:
            raise RuntimeError("Wrong EEPROM response length")
        return data

    def dump(self):
        return b"".join(self.read(a) for a in range(0, 2048, 16))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    board = Board(args.port)
    try:
        assert board.command("STOP", "STOP") == "STOP OK MOTOR_DISABLED"
        for text, expected in (
            ("LED ON", "LED requested=ON ODR=0 IDR=0"),
            ("LED OFF", "LED requested=OFF ODR=1 IDR=1"),
            ("EE SCAN", "EE BANK_MASK=FF"),
            ("EE TEST", "EE POLICY_TEST=PASS NO_PHYSICAL_WRITE"),
            ("MOTOR TEST", "MOTOR LOGIC_TEST=PASS"),
        ):
            result = board.command(text, expected.split("=")[0])
            print(result, flush=True)
            assert result == expected
        for text, prefix in (("INPUTS", "INPUTS"), ("ADC", "ADC"),
                             ("EE STATUS", "EE"), ("MOTOR STATUS", "MOTOR phase")):
            print(board.command(text, prefix), flush=True)
        for i in range(100):
            token = f"{i}-{secrets.token_hex(4)}"
            assert board.command(f"PING {token}", "PONG ") == f"PONG {token}"
        result = board.command("UART", "UART")
        print(result)
        assert result == "UART errors=0 drops=0"
        print("PASS non-motion checks; EEPROM writes unavailable")
    finally:
        board.serial.close()


if __name__ == "__main__":
    main()
