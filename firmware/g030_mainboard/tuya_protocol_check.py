"""Exercise the flashed SDK/parser and button logic via SWD, module TX isolated.
Resets to live TuyaApp on exit. No Tuya flash or authorization operations.
"""
import argparse
import json
import time
from pathlib import Path
from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper

p=argparse.ArgumentParser()
p.add_argument('--elf',type=Path,required=True)
p.add_argument('--probe',required=True)
p.add_argument('--product',type=Path,required=True)
a=p.parse_args()
product=json.loads(a.product.read_text(encoding='utf-8-sig'))
dps={x['id']:x for x in product['Dp_Data']}
assert dps[1]['property']['range']==['open','stop','close','continue']
assert dps[2]['property']=={'max':100,'min':0,'scale':0,'step':1,'type':'value','unit':'%'}
assert dps[101]['mode']=='ro'
with a.elf.open('rb') as f:
    syms=ELFFile(f).get_section_by_name('.symtab')
    names=['app_test_'+x for x in ('mode','ack','seq','done','length','tx_length','input','output','now','pressed','button_result')]
    addr={n:syms.get_symbol_by_name(n)[0]['st_value'] for n in names}
s=ConnectHelper.session_with_chosen_probe(unique_id=a.probe,blocking=False,options={
    'no_config':True,'target_override':'stm32g030c8tx','frequency':50000,
    'connect_mode':'attach','auto_unlock':False,'resume_on_disconnect':False,
    'cmsis_dap.limit_packets':True,'cmsis_dap.deferred_transfers':False,
    'pack.debug_sequences.enable':True,
    'pack.debug_sequences.disabled_sequences':['DebugCoreStart','DebugCoreStop','DebugDeviceUnlock'],
    'reset_type':'sysresetreq'})
if s is None:raise RuntimeError('Probe missing')
with s:
    t=s.target
    def put(name,value): t.write32(addr['app_test_'+name],value);t.flush()
    def get(name):return t.read32(addr['app_test_'+name])
    def wait(name,value):
        end=time.monotonic()+3
        while get(name)!=value:
            if time.monotonic()>end:raise TimeoutError(name)
    def mode(n):put('mode',n);wait('ack',n)
    def run():
        seq=get('seq')+1;put('seq',seq);wait('done',seq)
    def frame(cmd,data=b''):
        b=bytes([0x55,0xaa,0,cmd,len(data)>>8,len(data)&255])+data
        return b+bytes([sum(b)&255])
    def send(raw):
        for off in range(0,len(raw),64):t.write_memory_block8(addr['app_test_input']+off,raw[off:off+64])
        put('length',len(raw));run()
        length=get('tx_length');assert length<512
        out=bytes(t.read_memory_block8(addr['app_test_output'],length));result=[]
        while out:
            assert out[:3]==b'\x55\xaa\x03',out.hex()
            size=int.from_bytes(out[4:6],'big')+7
            assert len(out)>=size and sum(out[:size-1])&255==out[size-1]
            result.append((out[3],out[6:size-1]));out=out[size:]
        return result
    def req(c,d=b''):return send(frame(c,d))
    def dp(i,k,v):return bytes([i,k,0,len(v)])+v
    def reports(rows):
        assert all(c==7 for c,d in rows)
        return {d[0]:int.from_bytes(d[4:],'big') for c,d in rows}
    try:
        mode(1)
        assert req(0)[0][1] in (b'\0',b'\1')
        assert req(0)==[(0,b'\1')]
        info=json.loads(req(1)[0][1]);assert info['p']==product['Pro_Key']
        assert req(2)==[(2,b'')]
        assert reports(req(8))=={1:1}
        for v in (0,1,37,62,99,100):
            assert reports(req(6,dp(2,2,v.to_bytes(4,'big'))))=={}
        for v in (101,0xffffffff):
            assert reports(req(6,dp(2,2,v.to_bytes(4,'big'))))=={}
        for v in range(4):assert reports(req(6,dp(1,4,bytes([v]))))=={1:1}
        for payload in (dp(101,2,b'\0\0\0\x32'),dp(99,4,b'\0'),dp(2,4,b'\0'),b'\x02\x02\x01\x00',b'\x02'):
            assert req(6,payload)==[]
            assert req(0)==[(0,b'\1')]
        bad=bytearray(frame(6,dp(2,2,b'\0\0\0\x14')));bad[-1]^=1
        assert send(bytes(bad))==[]
        assert reports(req(8))=={1:1}
        print('PASS JSON PID/DP schema, heartbeat, product, mode, queries, unsupported percentage rejection, malformed/checksum/RO rejection',flush=True)
        print('BOUNDARY: isolated parser cannot drive motor; DP2 unsupported, no fabricated DP101',flush=True)
        def step(now,pressed):
            put('now',now&0xffffffff);put('pressed',pressed);run();return get('button_result')
        put('now',0);put('pressed',0);mode(2)
        assert step(10,1)==0 and step(20,0)==0 # bounce
        assert step(100,1)==0 and step(130,1)==0
        assert step(5129,1)==0 and step(5130,1)==1 # exact 5s after debounce
        assert step(20000,1)==0 # one-shot
        assert step(20001,0)==0 and step(20031,0)==0
        assert step(21000,1)==0 and step(21030,1)==0 and step(26030,1)==1
        mode(1);put('now',0);put('pressed',1);mode(2)
        assert step(10000,1)==0 # boot held cannot pair
        assert step(10001,0)==0 and step(10031,0)==0
        start=0xffffff00
        assert step(start,1)==0 and step(start+30,1)==0
        assert step(start+5029,1)==0 and step(start+5030,1)==1
        print('PASS actual button FSM: bounce, 4999/5000ms, held once, release rearm, boot-held lockout, tick wrap',flush=True)
    finally:
        t.reset();print('RESET to live TuyaApp',flush=True)
