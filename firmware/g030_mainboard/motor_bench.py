"""Explicit unloaded motor bench test. Each run is capped by firmware at 400 steps.
No EEPROM writes. Requires --run-motor to move; default only checks on-demand reads.
"""
import argparse
import hashlib
import re
import time
from pathlib import Path
from basic_check import Board


def fields(line):
    return {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)(?=\s|$)", line)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--run-motor", action="store_true")
    parser.add_argument("--eeprom-baseline", type=Path, required=True)
    args = parser.parse_args()
    original = args.eeprom_baseline.read_bytes()
    assert len(original) == 2048
    board = Board(args.port)

    def cmd(text, prefix):
        result = board.command(text, prefix)
        print(result, flush=True)
        return result

    try:
        cmd("STOP", "STOP")
        assert cmd("EE TEST", "EE POLICY") == "EE POLICY_TEST=PASS NO_PHYSICAL_WRITE"
        assert cmd("MOTOR TEST", "MOTOR LOGIC") == "MOTOR LOGIC_TEST=PASS"
        before = board.dump()
        assert before == original, "EEPROM differs from baseline; stop before motor test"
        cmd("EE STATUS", "EE ")
        cmd("ADC", "ADC ")
        cmd("INPUTS", "INPUTS ")
        if args.run_motor:
            for direction in ("F", "R"):
                assert cmd(f"MOTOR RUN {direction}", "MOTOR RUN") == "MOTOR RUN=ACCEPTED"
                deadline = time.monotonic() + 4
                finished = False
                while time.monotonic() < deadline:
                    state = fields(cmd("MOTOR STATUS", "MOTOR phase"))
                    if state["phase"] in (0, 4):
                        finished = True
                        if state["reason"] != 1 or state["count"] != 400:
                            raise RuntimeError("Run stopped abnormally; do not retry motion")
                        assert state["EN"] == state["SLEEP"] == state["LOW"] == 0
                        break
                    cmd("ADC", "ADC ")
                    time.sleep(0.15)
                if not finished:
                    raise RuntimeError("Completion timeout")
                cmd("ADC", "ADC ")
                time.sleep(1)
        # Status is RAM-only. Waiting must not trigger physical EEPROM access.
        initial = fields(cmd("EE STATUS", "EE "))
        time.sleep(15)
        current = fields(cmd("EE STATUS", "EE "))
        assert current == initial, "Unexpected background EEPROM activity"
        assert current["attempts"] == current["writes"] == current["errors"] == 0
        assert board.dump() == original
        print("PASS EEPROM unchanged SHA256=" + hashlib.sha256(original).hexdigest(), flush=True)
        for text in ("EE W 0 " + "00" * 16, "EE ERASE", "EE FILL FF"):
            try:
                board.command(text, "SHOULD_NOT_SUCCEED")
            except RuntimeError as error:
                print("PASS rejected", str(error), flush=True)
            else:
                raise AssertionError(text)
        cmd("UART", "UART ")
        print("PASS bounded bench/read-only checks; rotor motion remains unobserved", flush=True)
    finally:
        try:
            cmd("STOP", "STOP")
            cmd("MOTOR STATUS", "MOTOR phase")
        finally:
            board.serial.close()


if __name__ == "__main__":
    main()
