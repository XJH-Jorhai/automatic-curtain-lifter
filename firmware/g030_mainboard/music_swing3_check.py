"""Read-only checks after authorized startup playback; never starts motion."""
import argparse
import hashlib
import time
from pathlib import Path
from basic_check import Board
from motor_bench import fields


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", required=True)
    args = p.parse_args()
    b = Board(args.port)
    score = "T150 E4/4 E4/8 D4/8 C4/4 C4/8 A3/4 D4/8 E4/8 D4/8 E4/8 G3/4."
    try:
        boot = b.command("BOOT", "BOOT MUSIC ")
        print(boot, flush=True)
        assert boot == "BOOT MUSIC STARTED"
        for _ in range(12):
            state = b.command("MOTOR STATUS", "MOTOR phase")
            s = fields(state)
            if s["phase"] == 0:
                break
            time.sleep(0.5)
        print(state, flush=True)
        assert s["phase"] == 0 and s["reason"] == 1 and s["count"] > 4000
        assert s["note"] == 11 and s["nFAULT"] == 1
        assert s["EN"] == s["SLEEP"] == s["LOW"] == s["offset"] == 0
        initial = b.command("EE STATUS", "EE ")
        print(initial, flush=True)
        assert fields(initial)["reads"] == 2
        assert fields(initial)["writes"] == fields(initial)["attempts"] == 0
        assert b.command("MUSIC GET", score) == score
        test = b.command("TUNE TEST", "TUNE TEST")
        print(test, flush=True)
        assert test == "TUNE TEST=PASS NO_MOTION"
        assert b.command("EE STATUS", "EE ") == initial
        data = b.dump()
        record = Path(__file__).resolve().parents[2] / "hardware/test-records/2026-10-20-score-low5.bin"
        assert data == record.read_bytes()
        print("PASS corrected EEPROM image unchanged:", hashlib.sha256(data).hexdigest(), flush=True)
        idle = b.command("EE STATUS", "EE ")
        b.serial.reset_input_buffer()
        time.sleep(5)
        unsolicited = b.serial.read(b.serial.in_waiting or 1)
        assert not unsolicited, repr(unsolicited)
        assert b.command("EE STATUS", "EE ") == idle
        assert b.command("MOTOR STATUS", "MOTOR phase") == state
        print("PASS idle 5s: no unsolicited UART, EEPROM access, or repeated playback", flush=True)
        print(b.command("UART", "UART "), flush=True)
    finally:
        b.serial.close()


if __name__ == "__main__":
    main()
