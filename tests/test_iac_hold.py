"""Standalone hold-controller boundaries; --target compares actual Keil code.

This is not an OEM routine-parity test. Board-owner/register scenarios are in
test_ssc.c; these cases exercise the MCU ABI, unsigned time wrap and faults.
"""
import ctypes as C
import sys
from oem_harness import library

class Hold(C.Structure):
    _fields_=[('due',C.c_uint16),('stamp',C.c_uint16)]+[
        (n,C.c_uint8) for n in ['state','enabled','high','current','pending','fault','off_needed','response','fault_response']]
lib=library()
for name,args,result in [('start',[C.c_uint8,C.c_uint16],C.c_uint8),
                         ('stop',[],None),('fail',[C.c_uint8],None),
                         ('watch',[C.c_uint16,C.c_uint8],C.c_uint16),
                         ('complete',[C.c_uint8,C.c_uint16],None)]:
    f=getattr(lib,'iac_hold_'+name);f.argtypes=[C.POINTER(Hold),*args];f.restype=result
count=0
for high in range(256):
    s=Hold()
    assert lib.iac_hold_start(C.byref(s),high,65500)==(high in [0x12,0x13,0x1A,0x1B])
    count+=1
for initial in [0,1,32000,32760,65000,65500,65535]:
    for high in [0x12,0x13,0x1A,0x1B]:
        for stop in [False,True]:
            for status in [0,0x40,0x80,0xC0]:
                s=Hold();assert lib.iac_hold_start(C.byref(s),high,initial)
                assert lib.iac_hold_watch(C.byref(s),(s.due-1)&65535,0)==256
                started=s.due
                assert lib.iac_hold_watch(C.byref(s),started,0)==high
                if stop:lib.iac_hold_stop(C.byref(s))
                finished=(started+129)&65535
                lib.iac_hold_complete(C.byref(s),status,finished)
                expected=4 if status==0 else (3 if status==0x80 else 0)
                assert s.fault==expected
                if expected:
                    assert lib.iac_hold_watch(C.byref(s),finished,1)==256
                    assert lib.iac_hold_watch(C.byref(s),finished,0)==0x3F
                    lib.iac_hold_complete(C.byref(s),0xC0,(finished+129)&65535)
                    assert s.state==0 and not s.off_needed and s.fault==expected
                    assert s.fault_response==status
                elif stop:assert s.state==0 and not s.enabled
                else:
                    assert s.state==1 and s.due==(finished+331)&65535
                    assert lib.iac_hold_watch(C.byref(s),s.due,0)==((high&9)|0x24)
                count+=1
        for delay in [0,1,249,250,251,625,626,32767]:
            s=Hold();lib.iac_hold_start(C.byref(s),high,initial)
            now=(s.due+delay)&65535
            command=lib.iac_hold_watch(C.byref(s),now,0)
            assert command==(0x3F if delay>250 else high)
            assert s.fault==(7 if delay>250 else 0)
            count+=1
        for delay in [0,1,129,624,625,626,65535]:
            s=Hold();lib.iac_hold_start(C.byref(s),high,initial)
            started=s.due;lib.iac_hold_watch(C.byref(s),started,0)
            result=lib.iac_hold_watch(C.byref(s),(started+delay)&65535,0)
            assert result==(257 if delay>625 else 256)
            if delay>625:
                assert s.fault==1 and s.off_needed
                lib.iac_hold_complete(C.byref(s),0xC0,(started+delay)&65535)
                assert s.state==0 and s.off_needed # stale completion cannot rearm
            count+=1
print(f'PASS {count} standalone hold-state boundary cases'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
