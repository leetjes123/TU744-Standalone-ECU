"""Production compare ISRs and ADC model under live crank/coil scheduling.

The foreground fixture publishes an eligible knock configuration; analog volts
exercise the injected converter model. This does not model the physical CC195.
"""
import json
import re
import subprocess
from pathlib import Path
import test_oem_scheduler_target as target

ROOT=target.ROOT
out=ROOT/'build/knock-windows'; out.mkdir(exist_ok=True)
fields={'KnockConfig':['generation','epoch','mode','eligible','start','length','threshold','attack','maximum',
                       'hold','divisor','gain_code','debounce','latch_ms','stale_ms','manual_gain'],
        'KnockState':['mode','reference','gain','gain_code','retard','count','normal_count','fault','missed_windows',
                      'adc_timeouts','raw','qualified','hold','scheduled']}
keys=[(s,f) for s,fs in fields.items() for f in fs]
probe=out/'layout.c'
probe.write_text('#include "knock.h"\n#include <stddef.h>\nconst u16 layout[]={'+
                 ','.join(f'offsetof({s},{f})' for s,f in keys)+'};\n')
r=subprocess.run([str(target.keil/'BIN/C166.EXE'),str(probe),'LARGE','MOD167','SRC',
                  f'INCDIR({ROOT/"include"})'],cwd=out,capture_output=True,text=True)
assert r.returncode==0,r.stdout
layout=dict(zip(keys,[int(v,16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H',probe.with_suffix('.SRC').read_text())]))
cfg,state=target.variables['knock_config'],target.variables['knock']
def set_(m,s,f,v,w=1): target.put(m,(cfg if s=='KnockConfig' else state)+layout[s,f],v,w)
def get(m,f,w=1): return sum(m.mem.read8(state+layout['KnockState',f]+i)<<(8*i) for i in range(w))

reports=[]
for rpm in [600,1000,2000,4000,6400,10000,12000]:
    for mode in [1,2]:
        run=target.Run(rpm=rpm,traffic=True)
        m=run.m
        # Run's fixture clears startup interrupt requests. Drain the associated
        # initial scan result too, so ADWR does not wait for an acknowledged IRQ.
        m.mem.read16(0xFEA0)
        m.adc.set_volts(15,468*5/1023)
        m.mem.write16(0xFFC6,m.mem.read16(0xFFC6)|0x006E) # DP3: gain/test/sensor outputs
        m.mem.write16(0xFFD6,m.mem.read16(0xFFD6)|0x0021) # DP8: MF/BF2 outputs
        target.put(m,run.ecu+target.layout['Ecu,cal']+target.layout['Calibration,generation'],1)
        for f,v,w in [('mode',mode,1),('reference',32,1),('gain',4,1),('gain_code',5,1)]: set_(m,'KnockState',f,v,w)
        for f,v,w in [('generation',1,2),('mode',mode,1),('eligible',1,1),('start',20,1),('length',20,1),
                      ('threshold',40,1),('attack',4,1),('maximum',16,1),('hold',4,2),('divisor',16,1),
                      ('debounce',3,1),('latch_ms',500,2),('stale_ms',150,2),('manual_gain',1,1)]:
            set_(m,'KnockConfig',f,v,w)
        for i,v in enumerate([0,1,2,3,5,6,7]): m.mem.write8(cfg+layout['KnockConfig','gain_code']+i,v)
        def foreground():
            epoch=run.value(run.rotation,'Rotation','epoch')
            set_(m,'KnockConfig','epoch',epoch,2)
        run.foreground_hook=foreground
        report=run.run_revolutions(8)
        counts=get(m,'count',4)
        report['knock']={f:get(m,f,4 if f in ('count','normal_count') else 2 if f in ('raw','missed_windows','adc_timeouts','hold') else 1)
                         for f in fields['KnockState'] if f not in ('mode','reference','gain','gain_code')}
        gates=[(t,v) for t,p,b,v in m.ports.events if p=='P8' and b==0]
        report['knock']['gate_edges']=gates
        report['adc'] = m.adc.snapshot()
        report['adc_irq'] = m.intctl.regs['ADCIC']
        assert counts>=5 and not get(m,'fault'),report
        assert get(m,'raw',2)==468 and get(m,'retard')==(16 if mode==2 else 0),report
        assert len(gates)>=10,report
        assert not report['inhibits'] and not report['capture_overruns'],report
        assert all(len(report['outputs'][pin])>=4 for pin in ['P2.0','P2.1']),report
        # Compare actual fire edges on BOTH coils to the existing zero-retard
        # reference. Keep the scheduler's established measured jitter budget.
        expected_us = (12 if mode==2 else 0) * 60e6 / rpm / 360
        tolerance = m.gen['gen0'].slot_cycles()/160 + 10
        assert all(abs(errors[-1]-expected_us)<=tolerance for errors in report['fire_error_us'].values()),report
        reports.append(report)
        print(f'PASS windows {rpm} RPM mode={mode} samples={counts} retard={get(m,"retard")*.75}',flush=True)
(out/'results.json').write_text(json.dumps(dict(hex_sha256=target.HEX_SHA256,reports=reports,scope=__doc__),indent=2))
