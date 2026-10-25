"""Read-only audit of effective pyOCD configuration and repeatable Flash reads."""
import argparse
import faulthandler
import hashlib
import sys
import pyocd
from pyocd.core.session import Session
from pyocd.core.helpers import ConnectHelper

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--probe", required=True)
args = parser.parse_args()
faulthandler.dump_traceback_later(50, exit=True)
options = {
    "no_config": True, "target_override": "stm32g030c8tx",
    "frequency": 50000, "connect_mode": "attach", "auto_unlock": False,
    "resume_on_disconnect": False, "cmsis_dap.prefer_v1": False,
    "cmsis_dap.limit_packets": True, "cmsis_dap.deferred_transfers": False,
    "pack.debug_sequences.enable": True,
    "pack.debug_sequences.disabled_sequences": [
        "DebugCoreStart", "DebugCoreStop", "DebugDeviceUnlock"],
}
print("Python", sys.executable, "pyOCD", pyocd.__version__, pyocd.__file__, flush=True)
# Ensure discovery does not load implicit YAML before the target Session exists.
discovery = Session(None, options=options)
probe = ConnectHelper.choose_probe(blocking=False,
    unique_id=args.probe)
if probe is None:
    raise SystemExit("No probe")
print("Selected", probe.unique_id, flush=True)
with Session(probe, options=options) as session:
    print("Backend", type(probe._link._interface).__name__, flush=True)
    print("User script path", session._user_script_namespace.get("__file__"), flush=True)
    for key in options:
        print(key, session.options.get(key), flush=True)
    target = session.target
    print("CPUID", hex(target.read32(0xE000ED00)),
          "DBGMCU", hex(target.read32(0x40015800)),
          "OPTR", hex(target.read32(0x40022020)), flush=True)
    first = None
    for trial in range(3):
        data = bytearray()
        for offset in range(0, 4096, 64):
            data.extend(target.read_memory_block8(0x08000000 + offset, 64))
        if first is None:
            first = data
        elif first != data:
            raise SystemExit("Readback changed")
        print("PASS repeated read", trial + 1, hashlib.sha256(data).hexdigest(), flush=True)
    print("CPU", target.get_state(), flush=True)
faulthandler.cancel_dump_traceback_later()
