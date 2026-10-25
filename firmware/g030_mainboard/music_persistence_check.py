"""Commission the requested score, or verify it after USER-operated power cycle.

Does not send a playback command. Firmware plays once during startup.
The save phase writes only via the production Music_SaveScore API.
"""
import argparse
import hashlib
import time
import zlib
from pathlib import Path
from basic_check import Board
from motor_bench import fields

SCORE = "T150 E4/4 E4/8 D4/8 C4/4 C4/8 A3/4 D4/8 E4/8 D4/8 E4/8 G4/4."
RECORDS = Path(__file__).resolve().parents[2] / "hardware" / "test-records"


def check_record(data):
    r = data[:208]
    assert r[:4] == b"MSC1" and r[4] == 1 and r[5] == len(SCORE)
    assert r[16:16 + len(SCORE) + 1] == SCORE.encode() + b"\0"
    assert int.from_bytes(r[12:16], "little") == zlib.crc32(r[4:12] + r[16:])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--phase", choices=["save", "after-power"], required=True)
    args = parser.parse_args()
    b = Board(args.port)
    try:
        boot = b.command("BOOT", "BOOT MUSIC ")
        print(boot, flush=True)
        # Startup playback is finite. Merely wait for idle; never trigger it.
        for _ in range(12):
            state = fields(b.command("MOTOR STATUS", "MOTOR phase"))
            if state["phase"] == 0:
                break
            time.sleep(0.5)
        assert state["phase"] == 0 and state["EN"] == state["SLEEP"] == 0
        initial = b.command("EE STATUS", "EE ")
        print("INITIAL", initial, flush=True)
        stats = fields(initial)
        if args.phase == "save":
            assert stats["writes"] == stats["attempts"] == 0
            before = b.dump()
            assert before == b"\xff" * 2048, "Unexpected EEPROM data; stop for review"
            backup = RECORDS / "2026-10-20-score-before.bin"
            backup.write_bytes(before)
            assert b.command("MUSIC SAVE " + SCORE, "MUSIC SAVED") == "MUSIC SAVED"
            saved = b.command("EE STATUS", "EE ")
            print("SAVED", saved, flush=True)
            assert fields(saved)["attempts"] == 1 and fields(saved)["writes"] > 0
            for _ in range(3):
                assert b.command("MUSIC SAVE " + SCORE, "MUSIC SAVED") == "MUSIC SAVED"
            assert b.command("EE STATUS", "EE ") == saved
            print("PASS three duplicate saves: all I2C counters unchanged", flush=True)
            assert b.command("MUSIC GET", SCORE) == SCORE
            after = b.dump()
            check_record(after)
            assert after[208:] == before[208:]
            (RECORDS / "2026-10-20-score-saved.bin").write_bytes(after)
        else:
            assert boot == "BOOT MUSIC STARTED"
            assert stats["reads"] == 2 and stats["writes"] == stats["attempts"] == 0
            assert state["reason"] == 1 and state["count"] > 0 and state["offset"] == 0
            assert b.command("MUSIC GET", SCORE) == SCORE
            assert b.command("EE STATUS", "EE ") == initial
            after = b.dump()
            check_record(after)
            assert after == (RECORDS / "2026-10-20-score-saved.bin").read_bytes()
            print("PASS power-cycle record unchanged; boot read twice, wrote zero; playback finished",
                  flush=True)
        before_idle = b.command("EE STATUS", "EE ")
        idle_motor = b.command("MOTOR STATUS", "MOTOR phase")
        time.sleep(5)
        assert b.command("EE STATUS", "EE ") == before_idle
        assert b.command("MOTOR STATUS", "MOTOR phase") == idle_motor
        print("PASS idle: no background EEPROM access or repeated playback", flush=True)
        print("EEPROM SHA256", hashlib.sha256(after).hexdigest(), flush=True)
        print(idle_motor, flush=True)
    finally:
        b.serial.close()


if __name__ == "__main__":
    main()
