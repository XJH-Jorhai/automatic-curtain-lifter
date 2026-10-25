"""No-motion parser/oscillation tests. Never sends a valid playback command."""
import argparse
from basic_check import Board
from motor_bench import fields


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    board = Board(args.port)
    try:
        before = fields(board.command("MOTOR STATUS", "MOTOR phase"))
        assert before["phase"] == 0 and before["EN"] == before["SLEEP"] == 0
        result = board.command("TUNE TEST", "TUNE TEST")
        print(result, flush=True)
        assert result == "TUNE TEST=PASS NO_MOTION"
        valid = (
            ("c4f4g4", 3, 1590, 262, 392),
            ("C4 F4 G4", 3, 1590, 262, 392),
            ("T120 C#4/8 Db4/8 Bb3/4 R/4. P/16", 5, 2025, 277, 0),
            ("T150 E4/4 E4/8 D4/8 C4/4 C4/8 A3/4 D4/8 E4/8 D4/8 E4/8 G3/4.",
             11, 3530, 330, 196),
            ("T300 " + "R/32" * 32, 32, 1760, 0, 0),
        )
        for score, count, ms, first, last in valid:
            line = board.command("CHECK " + score, "TUNE VALID")
            print(line, flush=True)
            f = fields(line)
            assert (f["notes"], f["ms"], f["first"], f["last"]) == (count, ms, first, last)
        for command in ("CHECK C2", "CHECK B#5", "CHECK C4/0", "CHECK T0 C4",
                        "CHECK T40 C4/1 C4/1", "CHECK T300 " + "R/32" * 33,
                        "CHECK C4?", "CHECK " + "C4" * 100,
                        "PLAY C4?", "PLAY T0 C4"):
            try:
                board.command(command, "SHOULD_NOT_SUCCEED")
            except RuntimeError as e:
                print("PASS rejected:", e, flush=True)
            else:
                raise AssertionError(command)
        after = fields(board.command("MOTOR STATUS", "MOTOR phase"))
        assert after["count"] == before["count"] and after["phase"] == 0
        assert after["EN"] == after["SLEEP"] == after["LOW"] == after["offset"] == 0
        print("PASS motor remains idle; no playback, no STEP count change", flush=True)
        print(board.command("EE STATUS", "EE "), flush=True)
        print(board.command("UART", "UART "), flush=True)
    finally:
        board.serial.close()


if __name__ == "__main__":
    main()

