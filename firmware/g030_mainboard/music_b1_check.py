"""Explicit B1 provisioning or post-boot readback; never sends play commands."""
import argparse
import hashlib
import json
import time
import zlib
from pathlib import Path
from basic_check import Board
from motor_bench import fields

ROOT = Path(__file__).resolve().parents[2]
RECORDS = ROOT / "hardware/test-records"
SCORE = "T232 C4/16 E4/8 F#4/16 R/16 Ab4/8 G4/16 R/16 B4/16 C5/4"


def validate(data):
    valid = []
    for slot in range(2):
        r = data[slot * 256:slot * 256 + 208]
        if r[:4] != b"MSC1" or r[4] != 1:
            continue
        if int.from_bytes(r[12:16], "little") != zlib.crc32(r[4:12] + r[16:]):
            continue
        valid.append((int.from_bytes(r[8:12], "little"), slot, r[16:16 + r[5]].decode()))
    assert valid
    latest = max(valid)
    assert latest[2] == SCORE
    return latest[1]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", required=True)
    p.add_argument("--phase", choices=["save", "verify"], required=True)
    a = p.parse_args()
    manifest = json.loads((ROOT / "outputs/startup-sound-preview/scores.json").read_text(encoding="utf-8"))
    assert next(x for x in manifest if x["id"] == "B1")["score"] == SCORE
    b = Board(a.port)
    try:
        for _ in range(12):
            state = b.command("MOTOR STATUS", "MOTOR phase")
            if fields(state)["phase"] == 0:
                break
            time.sleep(.5)
        s = fields(state)
        assert s["phase"] == 0 and s["EN"] == s["SLEEP"] == 0
        print(state, flush=True)
        print(b.command("BOOT", "BOOT MUSIC "), flush=True)
        initial = b.command("EE STATUS", "EE ")
        print("INITIAL", initial, flush=True)
        if a.phase == "save":
            before = b.dump()
            assert before == (RECORDS / "2026-10-20-score-low5.bin").read_bytes()
            print(b.command("CHECK " + SCORE, "TUNE VALID"), flush=True)
            assert b.command("MUSIC SAVE " + SCORE, "MUSIC SAVED") == "MUSIC SAVED"
            saved = b.command("EE STATUS", "EE ")
            print("SAVED", saved, flush=True)
            for _ in range(3):
                assert b.command("MUSIC SAVE " + SCORE, "MUSIC SAVED") == "MUSIC SAVED"
            assert b.command("EE STATUS", "EE ") == saved
            assert b.command("MUSIC GET", SCORE) == SCORE
            after = b.dump()
            slot = validate(after)
            base = slot * 256
            assert before[:base] == after[:base] and before[base + 208:] == after[base + 208:]
            (RECORDS / "2026-10-20-score-b1.bin").write_bytes(after)
            assert b.command("MOTOR STATUS", "MOTOR phase") == state
            print("PASS B1 saved; three duplicate saves zero I2C; no playback", flush=True)
        else:
            assert s["reason"] == 1 and s["note"] == 9 and s["count"] > 0
            assert s["LOW"] == s["offset"] == 0 and s["nFAULT"] == 1
            assert fields(initial)["reads"] == 2 and fields(initial)["writes"] == fields(initial)["attempts"] == 0
            assert b.command("MUSIC GET", SCORE) == SCORE
            assert b.command("TUNE TEST", "TUNE TEST") == "TUNE TEST=PASS NO_MOTION"
            assert b.command("EE STATUS", "EE ") == initial
            after = b.dump()
            validate(after)
            assert after == (RECORDS / "2026-10-20-score-b1.bin").read_bytes()
            print("PASS B1 boot completed; parser/oscillation/storage self-tests; EEPROM unchanged", flush=True)
        idle = b.command("EE STATUS", "EE ")
        b.serial.reset_input_buffer()
        time.sleep(5)
        assert not b.serial.read(b.serial.in_waiting or 1)
        assert b.command("EE STATUS", "EE ") == idle
        assert b.command("MOTOR STATUS", "MOTOR phase") == state
        print("PASS idle: no unsolicited UART, EEPROM access, or repeat playback", flush=True)
        print("EEPROM SHA256", hashlib.sha256(after).hexdigest(), flush=True)
    finally:
        b.serial.close()


if __name__ == "__main__":
    main()
