#include "tune_transfer.h"
#include <algorithm>
#include <cstring>
void TuneTransfer::fail(EcuProtocol& ecu,const char* reason) {
    if(transaction) ecu.command(0x23);
    transaction=false; phase=Phase::Failed; message=reason;
}
bool TuneTransfer::read(EcuProtocol& ecu,bool allowEmpty) {
    allowUncalibrated=allowEmpty;
    Capabilities c;
    if(busy() || !ecu.capabilities(c)) { message="Unsupported or unavailable calibration protocol"; return false; }
    generation=c.generation; reading=true; transaction=false; pos=0;
    phase=Phase::Reading; message="Reading calibration"; return true;
}
bool TuneTransfer::write(EcuProtocol& ecu,const CalBuffer& cal,const unsigned char* baseline,bool synced) {
    if(busy()) return false;
    if(!synced) { message="Read the ECU calibration before writing"; return false; }
    const auto issues=ValidateCalibration(cal);
    if(!issues.empty()) { message=issues.front().message; return false; }
    Capabilities c; MonitorData m; EcuStatus s;
    if(!ecu.capabilities(c) || !ecu.readMonitor(m) || !ecu.status(s)) { message="Could not check ECU state"; return false; }
    if((m.flags&0xA0) || s.service) { message="ECU is saving, in a transaction, or needs a key cycle"; return false; }
    ranges=BuildDirtyRanges(cal.data,baseline,true);
    if(ranges.empty()) { message="Active calibration already matches"; return false; }
    for(const auto& r:ranges) for(int i=r.start;i<r.end;++i)
        if(IsStructuralOffset(i) && (m.rpm || (m.state&7) || s.iacState==1)) {
            message="Structural settings require a stopped engine and completed idle homing"; return false;
        }
    generation=c.generation;
    live=ranges.back().end<=0x400;
    if(live) ranges=SplitLiveWrites(ranges);
    std::copy(cal.data,cal.data+CAL_SIZE,image.begin());
    range=0; pos=ranges[0].start; transaction=false; reading=false;
    phase=live ? Phase::Writing : Phase::Begin;
    message=live ? "Writing live map cells" : "Starting calibration transaction";
    return true;
}
bool TuneTransfer::save(EcuProtocol& ecu) {
    Capabilities c; MonitorData m;
    if(busy() || !ecu.capabilities(c) || !ecu.readMonitor(m) || m.rpm || (m.state&7) || (m.flags&0xA0)) {
        message="Save requires a stopped engine and no pending calibration operation"; return false;
    }
    if(!ecu.command(0x24)) { message="ECU refused save; check status before retrying"; return false; }
    started=GetTickCount(); phase=Phase::Saving;
    message="Saving tune to ECU flash; key cycle required afterwards"; return true;
}
void TuneTransfer::step(EcuProtocol& ecu) {
    if(phase==Phase::Saving) {
        EcuStatus s;
        if(!ecu.status(s)) { fail(ecu,"Save status unavailable; persistent result is unknown. Reconnect and check."); return; }
        if(s.saveResult==1) { phase=Phase::Complete; message="Tune saved to ECU flash. Key cycle required."; }
        else if(s.saveResult) fail(ecu,s.saveResult==2 ? "Save conflict or timeout; key cycle required" : "Flash save failed; key cycle required");
        else if(GetTickCount()-started>15000) fail(ecu,"Save timed out; persistent result is unknown. Reconnect and check.");
        return;
    }
    if(phase==Phase::Begin) {
        // Treat a missing begin reply as possibly opened; abort on any failure.
        transaction=true;
        if(!ecu.command(0x21)) { fail(ecu,"Could not begin calibration transaction"); return; }
        phase=Phase::Writing; return;
    }
    if(phase==Phase::Writing) {
        const int count=std::min(live ? 16 : 32,ranges[range].end-pos);
        uint16_t g=0,edits=0;
        bool ok=live ? ecu.liveWrite(pos,count,image.data()+pos,g,edits) : ecu.writeChunk(pos,count,image.data()+pos);
        if(live && ok) {
            unsigned char check[16];
            ok=g==generation && ecu.readChunk(pos,count,check) && !memcmp(check,image.data()+pos,count);
        }
        if(!ok) { fail(ecu,"Write or readback failed; read the ECU again before further edits"); return; }
        pos+=count;
        if(pos==ranges[range].end) {
            if(++range<int(ranges.size())) pos=ranges[range].start;
            else if(live) {
                Capabilities c;
                if(!ecu.capabilities(c) || c.generation!=generation) {
                    fail(ecu,"Calibration generation changed during live edits; read ECU again"); return;
                }
                // Each map run was read back above. Do not turn a single-cell
                // live edit into a full 3 KiB transfer before monitoring resumes.
                phase=Phase::Complete;
                message="Live map edits written and verified; save to flash to keep them";
            }
            else phase=Phase::Commit;
        }
        return;
    }
    if(phase==Phase::Commit) {
        if(!ecu.command(0x22)) { fail(ecu,"Commit rejected or reply lost; read ECU to establish the active tune"); return; }
        transaction=false;
        Capabilities c;
        if(!ecu.capabilities(c) || c.generation!=uint16_t(generation+1)) {
            fail(ecu,"Unexpected calibration generation after commit; read ECU again"); return;
        }
        generation=c.generation; phase=Phase::Verify; pos=0; return;
    }
    if(phase==Phase::Reading || phase==Phase::Verify) {
        unsigned char check[128]; const int count=std::min(128,CAL_SIZE-pos);
        if(!ecu.readChunk(pos,count,check)) { fail(ecu,"Calibration read failed; local file preserved"); return; }
        if(reading) memcpy(image.data()+pos,check,count);
        else if(memcmp(image.data()+pos,check,count)) { fail(ecu,"Calibration readback differs; read ECU again"); return; }
        pos+=count;
        if(pos==CAL_SIZE) {
            Capabilities c;
            if((!allowUncalibrated && !HasSchemaMarker(image.data())) || !ecu.capabilities(c) || c.generation!=generation) {
                fail(ecu,"Schema or calibration generation changed during readback"); return;
            }
            phase=Phase::Complete;
            message=reading ? "Calibration read from ECU" : "Active tune written and verified; save to flash to keep it";
        }
    }
}
float TuneTransfer::progress() const { return std::clamp(pos/float(CAL_SIZE),0.0f,1.0f); }
