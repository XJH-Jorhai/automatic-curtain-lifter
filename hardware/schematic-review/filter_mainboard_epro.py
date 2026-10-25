"""Extract the mainboard from the saved V2 recovery project for V3 import.

The original recovery archive is left untouched.  This removes the older
breakout Board1 to avoid a name collision with the existing V3 Board1.
"""

import json
import zipfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
SOURCE = HERE / "Curtain_Lifter_V2_recovery_2026-09-28.epro"
TARGET = HERE / "Curtain_Lifter_Mainboard_RevA_V2_import.epro"
REMOVE_BOARD = "Board1"
KEEP_BOARD = "Curtain_Lifter_Mainboard_RevA"

with zipfile.ZipFile(SOURCE) as source:
    project = json.loads(source.read("project.json"))
    assert set(project["boards"]) == {REMOVE_BOARD, KEEP_BOARD}
    old = project["boards"].pop(REMOVE_BOARD)
    project["schematics"].pop(old["schematic"])
    project["pcbs"].pop(old["pcb"])
    omit = {
        "project.json",
        f"PCB/{old['pcb']}.epcb",
    }
    with zipfile.ZipFile(TARGET, "w") as target:
        for entry in source.infolist():
            if entry.filename in omit or entry.filename.startswith(
                f"SHEET/{old['schematic']}/"
            ):
                continue
            target.writestr(entry, source.read(entry.filename))
        target.writestr(
            "project.json",
            json.dumps(project, ensure_ascii=False, separators=(",", ":")).encode(),
        )

with zipfile.ZipFile(TARGET) as archive:
    check = json.loads(archive.read("project.json"))
    assert set(check["boards"]) == {KEEP_BOARD}
    assert len(check["schematics"][check["boards"][KEEP_BOARD]["schematic"]]["sheets"]) == 4
    print(TARGET, TARGET.stat().st_size, list(check["boards"]))
