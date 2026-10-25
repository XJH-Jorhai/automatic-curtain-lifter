"""4KB exact-byte UART2 tests, isolated from the module through SWD mailbox.

No module flashing command is sent. Resets STM32 to normal bridge on exit.
"""
import argparse
import hashlib
import random
import time
from pathlib import Path
import serial
from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper

p = argparse.ArgumentParser()
p.add_argument('--elf', type=Path, required=True)
p.add_argument('--probe', required=True)
p.add_argument('--port', required=True)
p.add_argument('--dtr', action='store_true')
p.add_argument('--driver-buffer', type=int, default=0)
p.add_argument('--paced-only', action='store_true')
p.add_argument('--burst-only', action='store_true', help='One 4KB input for USB tracing')
a = p.parse_args()
with a.elf.open('rb') as f:
    table = ELFFile(f).get_section_by_name('.symtab')
    addr = {n: table.get_symbol_by_name(n)[0]['st_value'] for n in
            ('bridge_test_mode', 'bridge_test_ack', 'bridge_test_count',
             'storage', 'bridge_stats')}
s = ConnectHelper.session_with_chosen_probe(unique_id=a.probe, blocking=False, options={
    'no_config': True, 'target_override': 'stm32g030c8tx', 'frequency': 50000,
    'connect_mode': 'attach', 'auto_unlock': False, 'resume_on_disconnect': False,
    'cmsis_dap.limit_packets': True, 'cmsis_dap.deferred_transfers': False,
    'pack.debug_sequences.enable': True,
    'pack.debug_sequences.disabled_sequences': ['DebugCoreStart','DebugCoreStop','DebugDeviceUnlock'],
    'reset_type': 'sysresetreq',
})
if s is None:
    raise SystemExit('No probe')
with s:
    t = s.target
    buf = addr['storage']
    def readbuf():
        return bytes(x for off in range(0,4096,64)
                     for x in t.read_memory_block8(buf+off,64))
    def mode(n):
        t.write32(addr['bridge_test_mode'], n)
        t.flush()
        until = time.monotonic()+2
        while t.read32(addr['bridge_test_ack']) != n:
            if time.monotonic()>until: raise RuntimeError('Mode ack timeout')
    def check(label, expected, actual):
        if expected != actual:
            diffs = [i for i,(x,y) in enumerate(zip(expected,actual)) if x!=y]
            print(f'FAIL {label}: length={len(actual)}, first mismatches={diffs[:16]}',flush=True)
            print('expected head',expected[:32].hex(),'actual head',actual[:32].hex(),flush=True)
            return False
        print('PASS',label,len(actual),'SHA256',hashlib.sha256(actual).hexdigest(),flush=True)
        return True
    port=serial.Serial(port=None,baudrate=115200,timeout=0.2,write_timeout=3)
    port.dtr=a.dtr;port.rts=False;port.port=a.port
    try:
        mode(3)  # idle diagnostics; module TX isolated before opening serial
        with port:
            if a.driver_buffer:
                port.set_buffer_size(rx_size=a.driver_buffer, tx_size=a.driver_buffer)
            time.sleep(0.5)
            port.reset_input_buffer()
            patterns=[(4096,0,bytes(range(256))*16),
                      (64,0.008,bytes(range(256))*16),
                      (256,0.03,random.Random(20261021).randbytes(4096)),
                      (4096,0,bytes([0x55,0xaa])*2048),
                      (4096,0,random.Random(20261021).randbytes(4096))]
            if a.paced_only:
                patterns = [item for item in patterns if item[1]]
            if a.burst_only:
                patterns = [(4096, 0, random.Random(20261021).randbytes(4096))]
            failures=0
            for trial,(chunk,pause,data) in enumerate(patterns):
                mode(1)
                before=t.read_memory_block32(addr['bridge_stats'],8)
                for off in range(0,4096,chunk):
                    written = port.write(data[off:off+chunk])
                    if written != len(data[off:off+chunk]):
                        raise RuntimeError(f'Short host write: {written}')
                    if pause: time.sleep(pause)
                port.flush()
                time.sleep(2)
                count=t.read32(addr['bridge_test_count'])
                after=t.read_memory_block32(addr['bridge_stats'],8)
                print('RX stats delta',[y-x for x,y in zip(before,after)],flush=True)
                failures += any(after[i] != before[i] for i in (2,3,4,5,6))
                mode(3)
                failures+=not check(f'{trial} chunk={chunk} PC->STM32 count={count}',data,readbuf()[:count])
                if a.burst_only:
                    # Module and STM32 UART TX are isolated in capture mode.
                    # Any bytes returned here originate in the serial adapter.
                    adapter_reply = port.read(port.in_waiting)
                    print('ADAPTER_REPLY', len(adapter_reply), repr(adapter_reply), flush=True)
                    continue
                # Use a different output pattern, loaded directly through SWD.
                output=bytes(x^0x3c for x in data)
                for off in range(0,4096,64):
                    t.write_memory_block8(buf+off,output[off:off+64])
                if not check(f'{trial} SWD output buffer',output,readbuf()):
                    raise RuntimeError('Invalid output test fixture')
                port.reset_input_buffer()
                mode(2)
                received=bytearray();deadline=time.monotonic()+3
                while len(received)<4096 and time.monotonic()<deadline:
                    received.extend(port.read(4096-len(received)))
                mode(3)
                failures+=not check(f'{trial} STM32->PC',output,bytes(received))
            for trial in range(0 if a.burst_only else 6):
                data=random.Random(20261021+trial).randbytes(4096)
                port.reset_input_buffer()
                mode(4)
                before=t.read_memory_block32(addr['bridge_stats'],8)
                for off in range(0,4096,64):
                    port.write(data[off:off+64])
                    time.sleep(0.008)
                received=port.read(4096)
                after=t.read_memory_block32(addr['bridge_stats'],8)
                mode(3)
                failures += not check(f'queue wraparound loopback {trial}',data,received)
                failures += any(after[i] != before[i] for i in (2,3,4,5,6))
            stats=t.read_memory_block32(addr['bridge_stats'],16)
            print('STATS',stats,flush=True)
            if failures:
                raise RuntimeError(f'Transport failures={failures}, see directional results')
    finally:
        t.reset()
        print('STM32 reset to normal bridge; serial closed',flush=True)
