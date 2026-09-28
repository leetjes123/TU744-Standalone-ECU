#include "diagnostics.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

bool DecodeDtc(const unsigned char* p,int n,DtcRecord& out) {
    if(n!=24 || p[0]==0 || p[0]>105) return false;
    DtcRecord r;
    std::copy(p,p+24,r.raw.begin());
    const unsigned descriptor=p[2]|unsigned(p[3])<<8, status=p[4]|unsigned(p[5])<<8;
    r.event=p[0]; r.subtype=(descriptor>>8)&15; r.firstSubtype=(descriptor>>12)&15;
    r.active=(descriptor&1)!=0; r.mil=(status&8)!=0; r.flashing=(status&16)!=0;
    auto label=LookupDtc(r.event,r.subtype), first=LookupDtc(r.event,r.firstSubtype);
    char fallback[40]; snprintf(fallback,sizeof(fallback),"Event %02X / %X",r.event,r.subtype);
    r.code=label.code?label.code:fallback;
    r.description=label.description?label.description:"Unmapped diagnostic event";
    snprintf(fallback,sizeof(fallback),"Event %02X / %X",r.event,r.firstSubtype);
    r.firstCode=first.code?first.code:fallback;
    r.coolant=int(p[9])-40; r.intake=int(p[10])-40; r.throttle=p[11]*.4f;
    r.map=p[12]; r.battery=p[13]*.1f; r.rpm=p[14]*32;
    r.speed=p[17]; r.mode=p[18]; r.trimAvailable=p[19]!=255; r.trim=p[19]*100.f/128;
    r.clock=unsigned(p[20])*256+p[21]; r.occurrences=p[22]; out=r; return true;
}
const char* ControllerFaultName(unsigned id) {
    static const char* names[]={"Unknown controller fault","Calibration validity","Throttle input quality",
        "Coolant input quality","Intake temperature input quality","Battery input quality","Manifold pressure input quality"};
    return names[id<=6?id:0];
}
void Diagnostics::fail(const char* why) {
    phase=Phase::Failed;
    message=clearResult==ClearResult::Unknown?"Clear acknowledgement lost; completion unconfirmed. ":
            clearResult==ClearResult::Accepted?"Clear accepted; refresh did not complete. ":"";
    message+=why;
}
void Diagnostics::retry() {
    if(++retries>2) { fail("Fault memory changed during the scan. Read again; previous results are retained."); return; }
    pending={}; slot=0; phase=Phase::Life;
}
void Diagnostics::start(bool clear,uint64_t now) {
    if(busy()) return;
    pending={}; count=slot=retries=0; deadline=now+20000; clearResult=ClearResult::None;
    phase=clear?Phase::CheckStopped:Phase::Life;
    message=clear?"Checking stopped-engine state...":"Reading fault memory...";
}
float Diagnostics::progress() const {
    if(!busy()) return 1;
    if(phase==Phase::Rows) return .1f+.3f*slot/(count?count:1);
    if(phase==Phase::VerifyRows) return .45f+.3f*slot/(count?count:1);
    if(phase==Phase::ControllerRows) return .8f+.15f*slot/7;
    return .05f;
}
void Diagnostics::step(EcuProtocol& ecu,uint64_t now) {
    if(!busy()) return;
    if(now>=deadline) { fail("Diagnostic request timed out. Read again to check fault memory."); return; }
    unsigned char req[2]={},b[32]={}; int n;
    switch(phase) {
    case Phase::CheckStopped: {
        TpsSnapshot t;
        if(!ecu.tpsSnapshot(t) || !(t.flags&2)) { fail("Clear requires a fresh stopped-engine report. Stop the engine and keep the key on."); return; }
        phase=Phase::Clear; break;
    }
    case Phase::Clear:
        req[0]=0x30; n=ecu.transact(req,1,b,sizeof(b));
        if(n==1 && b[0]==1) { clearResult=ClearResult::Rejected; fail("ECU rejected clear. Keep the key on, engine stopped and diagnostics ready."); return; }
        clearResult=(n==1 && b[0]==0)?ClearResult::Accepted:ClearResult::Unknown;
        // Never repeat a destructive command after an ambiguous acknowledgement.
        message=clearResult==ClearResult::Accepted?"Clear accepted. Refreshing fault memory...":"Clear acknowledgement lost. Reading memory without repeating the clear...";
        waitUntil=now+250; phase=Phase::Wait; break;
    case Phase::Wait: if(now>=waitUntil) phase=Phase::Life; break;
    case Phase::Life: case Phase::VerifyLife:
        req[0]=0x27; n=ecu.transact(req,1,b,sizeof(b));
        if(n!=24 || b[0]!=1 || b[20]>20) { fail("Unsupported or incomplete diagnostic status. Previous results are retained."); return; }
        if(!b[7] || !b[8] || !b[14] || b[9]) { fail("Diagnostic history is not ready. Read again after ECU initialization."); return; }
        if(phase==Phase::VerifyLife && count!=b[20]) { retry(); return; }
        pending.mil=b[21]!=0; pending.flashing=b[22]!=0; pending.dirty=b[15]!=0;
        pending.clearPending=b[18]!=0; pending.clearDurable=b[19]!=0; pending.journalResult=b[17];
        slot=0;
        if(phase==Phase::Life) { count=b[20]; pending.records.clear(); phase=count?Phase::Rows:Phase::VerifyLife; }
        else phase=count?Phase::VerifyRows:Phase::ControllerSummary;
        break;
    case Phase::Rows: case Phase::VerifyRows: {
        req[0]=0x29; req[1]=(unsigned char)slot;
        n=ecu.transact(req,2,b,sizeof(b)); DtcRecord r;
        if(!DecodeDtc(b,n,r)) { fail("Incomplete or invalid DTC record. Previous results are retained."); return; }
        if(phase==Phase::Rows) {
            for(const auto& old:pending.records) if(old.event==r.event) { retry(); return; }
            pending.records.push_back(r);
        } else if(r.raw!=pending.records[slot].raw) { retry(); return; }
        if(++slot==count) phase=phase==Phase::Rows?Phase::VerifyLife:Phase::ControllerSummary;
        break;
    }
    case Phase::ControllerSummary: case Phase::VerifyController: {
        req[0]=0x2B; n=ecu.transact(req,1,b,sizeof(b));
        if(n!=12 || b[0]!=1 || b[1]!=6 || b[2] || b[4] || (b[3]|b[5])&0xc0 || !b[6]) {
            fail("Controller fault summary is unavailable. Previous results are retained."); return;
        }
        if(phase==Phase::ControllerSummary) {
            activeMask=b[3]; controllerMask=b[5];
            if(activeMask&~controllerMask) { retry(); return; }
            pending.controller.clear(); slot=1; phase=Phase::ControllerRows;
        } else {
            if(activeMask!=b[3] || controllerMask!=b[5]) { retry(); return; }
            pending.valid=true; pending.capturedAt=now; snapshot=pending; phase=Phase::Complete;
            message=clearResult==ClearResult::Accepted?"Clear accepted; fault memory refreshed. Present faults can reappear. History is saved at key-off.":
                clearResult==ClearResult::Unknown?"Memory refreshed; clear acknowledgement was lost. Clear completion is unconfirmed.":"Fault memory read successfully.";
        }
        break;
    }
    case Phase::ControllerRows: {
        while(slot<=6 && !(controllerMask&(1u<<(slot-1)))) ++slot;
        if(slot>6) { phase=Phase::VerifyController; break; }
        req[0]=0x2C; req[1]=(unsigned char)slot; n=ecu.transact(req,2,b,sizeof(b));
        if(n!=16 || b[0]!=1 || b[1]!=slot || b[2]>1 || b[3]>1) { fail("Invalid controller fault record. Previous results are retained."); return; }
        if(!b[3] || bool(b[2])!=bool(activeMask&(1u<<(slot-1)))) { retry(); return; }
        auto word=[&](int at) { return unsigned(b[at])*256+b[at+1]; };
        auto wide=[&](int at) { return uint32_t(word(at))*65536+word(at+2); };
        ControllerFault r; r.id=slot; r.reason=b[4]; r.active=b[2]!=0; r.occurrences=word(6); r.first=wide(8); r.last=wide(12);
        pending.controller.push_back(r); ++slot; break;
    }
    default: break;
    }
}
