"""Verify boot and idle do not access EEPROM; perform one explicit 16-byte read."""
import argparse
import time
from basic_check import Board
from motor_bench import fields


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    board = Board(args.port)
    def status():
        line = board.command("EE STATUS", "EE ")
        print(line, flush=True)
        return fields(line)
    try:
        initial = status()
        assert initial["reads"] == initial["probes"] == initial["attempts"] == 0
        time.sleep(15)
        assert status() == initial, "Idle accessed EEPROM"
        print("PASS boot + 15s idle: no EEPROM access", flush=True)
        print(board.command("EE R 0", "EE DATA 0 "), flush=True)
        after = status()
        assert after["reads"] == 1 and after["writes"] == after["attempts"] == 0
        time.sleep(3)
        assert status() == after
        print("PASS one explicit read, no subsequent background accesses", flush=True)
        assert board.command("EE TEST", "EE POLICY") == "EE POLICY_TEST=PASS NO_PHYSICAL_WRITE"
        final = status()
        assert final["reads"] == 3 and final["attempts"] == final["writes"] == 0
        print("PASS update policy self-test; no physical writes", flush=True)
    finally:
        board.serial.close()


if __name__ == "__main__":
    main()
