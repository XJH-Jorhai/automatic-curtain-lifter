"""Check the current V3 EEPROM replacement against the user's prior V3 design."""

import json
from collections import defaultdict
from pathlib import Path


HERE = Path(__file__).resolve().parent
BEFORE = HERE / "Curtain_Lifter_Mainboard_v3_before_memory_change.enet"
AFTER = HERE / "Curtain_Lifter_Mainboard_v3.enet"


def read_parts(path):
    raw = json.loads(path.read_text(encoding="utf-8-sig"))["components"]
    return {part["props"]["Designator"]: part for part in raw.values()}


def connected_pin_groups(parts):
    nets = defaultdict(set)
    for designator, part in parts.items():
        for pin_number, pin in part["pinInfoMap"].items():
            if pin["net"]:
                nets[pin["net"]].add((designator, str(pin_number)))
    return {frozenset(pins) for pins in nets.values()}


before = read_parts(BEFORE)
after = read_parts(AFTER)
assert set(before) == set(after), "Component designators changed"

changes = []
for designator in sorted(before):
    old = before[designator]["props"]
    new = after[designator]["props"]
    for key in ("Supplier Part", "Manufacturer Part", "Supplier Footprint", "Value"):
        if old.get(key) != new.get(key):
            changes.append((designator, key, old.get(key), new.get(key)))

assert changes == [
    ("U4", "Supplier Part", "C31082", "C6480"),
    ("U4", "Manufacturer Part", "FM24CL16B-GTR", "AT24C16C-SSHM-T"),
], changes

before_groups = connected_pin_groups(before)
after_groups = connected_pin_groups(after)
assert before_groups == after_groups, "Electrical pin groups changed"

print(f"Components: {len(after)}")
print(f"Electrical pin groups: {len(after_groups)}")
print("Only U4 part identity changed; all pin connectivity is preserved.")
