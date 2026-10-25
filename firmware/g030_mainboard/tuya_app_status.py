"""Read-only running TuyaApp status via SWD. Does not reset or halt."""
import argparse
from pathlib import Path
from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper
p=argparse.ArgumentParser();p.add_argument('--elf',type=Path,required=True);p.add_argument('--probe',required=True);a=p.parse_args()
with a.elf.open('rb') as f:
    table=ELFFile(f).get_section_by_name('.symtab')
    names=['app_test_mode','bridge_rx_frames','bridge_rx_overflows','bridge_uart_errors','bridge_tx_errors','bridge_rejected_commands','pairing_press_events','pairing_button_down','pairing_request_ms','wifi_work_state','bridge_last_command']
    names += ['bridge_control_rx_count','bridge_stop_accepted','bridge_last_control','bridge_wire_tx_count','stop_update_flag']
    symbols={n:table.get_symbol_by_name(n)[0] for n in names}
    values={n:(x['st_value'],x['st_size']) for n,x in symbols.items()}
    tx_addr=table.get_symbol_by_name('bridge_wire_tx')[0]['st_value']
s=ConnectHelper.session_with_chosen_probe(unique_id=a.probe,blocking=False,options={
    'no_config':True,'target_override':'stm32g030c8tx','frequency':50000,
    'connect_mode':'attach','auto_unlock':False,'resume_on_disconnect':False,
    'cmsis_dap.limit_packets':True,'cmsis_dap.deferred_transfers':False,
    'pack.debug_sequences.enable':True,
    'pack.debug_sequences.disabled_sequences':['DebugCoreStart','DebugCoreStop','DebugDeviceUnlock']})
if s is None:raise RuntimeError('Probe missing')
with s:
    t=s.target;print('CPU',t.get_state())
    for n,(addr,size) in values.items():print(n,t.read8(addr) if size==1 else t.read32(addr))
    for name,base in [('module',0x40013800),('console',0x40004400)]:
        print(name,'BRR',t.read32(base+12),'ISR',hex(t.read32(base+28)))
    print('PC13 input',bool(t.read32(0x50000810)&(1<<13)))
    count=t.read32(values['bridge_wire_tx_count'][0])
    raw=bytes(x for off in range(0,512,64) for x in t.read_memory_block8(tx_addr+off,64))
    if count != t.read32(values['bridge_wire_tx_count'][0]):
        print('TX changed during snapshot; retry to inspect frames')
    else:
        raw=raw[:count] if count<512 else raw[count%512:]+raw[:count%512]
        while len(raw)>=7:
            if raw[:3]!=b'\x55\xaa\x03':raw=raw[1:];continue
            size=int.from_bytes(raw[4:6],'big')+7
            if len(raw)<size:break
            if sum(raw[:size-1])&255!=raw[size-1]:raw=raw[1:];continue
            if raw[3]==7:print('TX DP frame',raw[:size].hex(' '))
            raw=raw[size:]
