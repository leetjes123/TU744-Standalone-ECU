#include "protocol.h"
#include "protocol_codec.h"
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <windows.h>

// ============================================================
//  Core transact: send packet, read echo+response, parse
//  This matches the proven Python approach: read ALL bytes
//  (echo + response) as one blob, skip the echo by known
//  packet length, then parse the response portion.
// ============================================================

int EcuProtocol::transact(const unsigned char* payload,int payloadLen,unsigned char* response,int capacity,int timeoutMs) {
    lastFailure=LINK_OK;
    if(exchange) {
        const int n=exchange(payload,payloadLen,response,capacity,timeoutMs);
        if(n<0) lastFailure=LINK_TIMEOUT;
        return n;
    }
    auto fail=[this](uint8_t cause) { lastFailure=cause; return -1; };
    if(!port || !port->isOpen()) return fail(LINK_SERIAL_ERROR);
    unsigned char packet[128],raw[512];
    const int length=BuildRequestPacket(payload,payloadLen,packet,sizeof(packet));
    if(length<0) return fail(LINK_BAD_FRAME);
    PurgeComm(port->handle,PURGE_RXCLEAR);
    DWORD written=0;
    if(!WriteFile(port->handle,packet,length,&written,nullptr) || written!=DWORD(length)) return fail(LINK_SERIAL_ERROR);
    COMMTIMEOUTS timeouts={}; timeouts.ReadIntervalTimeout=MAXDWORD;
    timeouts.ReadTotalTimeoutConstant=1; timeouts.WriteTotalTimeoutConstant=500;
    if(!SetCommTimeouts(port->handle,&timeouts)) return fail(LINK_SERIAL_ERROR);
    int total=0; const DWORD start=GetTickCount();
    while(GetTickCount()-start<DWORD(timeoutMs) && total<int(sizeof(raw))) {
        COMSTAT state={}; DWORD errors=0;
        // Framing/parity/overrun errors: usually electrical noise on the K-line.
        if(!ClearCommError(port->handle,&errors,&state) || errors) return fail(LINK_SERIAL_ERROR);
        if(!state.cbInQue) { Sleep(1); continue; }
        DWORD count=0;
        const DWORD want=state.cbInQue<DWORD(sizeof(raw)-total)?state.cbInQue:DWORD(sizeof(raw)-total);
        if(!ReadFile(port->handle,raw+total,want,&count,nullptr)) return fail(LINK_SERIAL_ERROR);
        total+=int(count);
        // Accept adapters with or without local echo, never arbitrary payload
        // bytes that happen to resemble a frame header.
        int at=0;
        if(total && raw[0]==REQ_HEADER) {
            if(total<length) continue;
            if(memcmp(raw,packet,length)) return fail(LINK_ECHO_MISMATCH);
            at=length;
        }
        if(total>=at+2) {
            if(raw[at]!=RESP_HEADER) return fail(LINK_BAD_FRAME);
            if(total>=at+int(raw[at+1])+3) {
                const int n=ParseResponsePacket(raw+at,total-at,response,capacity);
                return n<0 ? fail(LINK_BAD_FRAME) : n;
            }
        }
    }
    return fail(LINK_TIMEOUT);
}

bool EcuProtocol::getVersion() {
    const unsigned char cmd=0;
    int n=transact(&cmd,1,reinterpret_cast<unsigned char*>(versionString),63);
    if(n<1) { versionString[0]=0; return false; }
    versionString[n]=0; return true;
}
static uint16_t word(const unsigned char* p) { return uint16_t((p[0]<<8)|p[1]); }
bool EcuProtocol::command(unsigned char cmd) {
    unsigned char out[1]; return transact(&cmd,1,out,1,1200)==1 && out[0]==0;
}
bool EcuProtocol::capabilities(Capabilities& out) {
    unsigned char cmd=0x20,b[10];
    if(transact(&cmd,1,b,10)!=10 || b[0]!=3 || b[1]!=5 || word(b+2)!=3072 || b[6]!=32 || b[7]!=128) return false;
    out.generation=word(b+4); return true;
}
bool EcuProtocol::status(EcuStatus& out) {
    unsigned char cmd=0x25,b[34];
    if(transact(&cmd,1,b,34)!=34) return false;
    out={word(b),word(b+4),word(b+6),b[8],b[9],b[10]}; return true;
}
bool EcuProtocol::readChunk(int offset,int len,unsigned char* out) {
    if(!out || offset<0 || len<1 || len>128 || offset>3072-len) return false;
    unsigned char cmd[]={4,(unsigned char)(offset>>8),(unsigned char)offset,(unsigned char)len};
    return transact(cmd,4,out,len)==len;
}
bool EcuProtocol::writeChunk(int offset,int len,const unsigned char* data) {
    if(!data || offset<0 || len<1 || len>32 || offset>3072-len) return false;
    unsigned char cmd[36]={5,(unsigned char)(offset>>8),(unsigned char)offset,(unsigned char)len},out[1];
    memcpy(cmd+4,data,len); return transact(cmd,4+len,out,1,1200)==1 && !out[0];
}
bool EcuProtocol::liveWrite(int offset,int len,const unsigned char* data,uint16_t& generation,uint16_t& edits) {
    if(!data || offset<0 || len<1 || len>16 || offset>1024-len || (offset&255)+len>256) return false;
    unsigned char cmd[20]={0x32,(unsigned char)(offset>>8),(unsigned char)offset,(unsigned char)len},out[5];
    memcpy(cmd+4,data,len);
    if(transact(cmd,4+len,out,5)!=5 || out[0]) return false;
    generation=word(out+1); edits=word(out+3); return true;
}
bool EcuProtocol::tpsSnapshot(TpsSnapshot& out) {
    unsigned char cmd=0x31,b[12];
    if(transact(&cmd,1,b,12)!=12 || b[0]!=1) return false;
    out={b[1],word(b+2),word(b+4),word(b+6),word(b+8),word(b+10)}; return true;
}
int EcuProtocol::readTpsAdc() {
    TpsSnapshot s;
    return tpsSnapshot(s) && (s.flags&3)==3 && !(s.flags&16) && s.ageMs<=100 && s.raw<=1023 ? s.raw : -1;
}
bool DecodeMonitor(const unsigned char* d,int len,MonitorData& out) {
    if(!d || !((len==40 && d[0]==2) || (len==44 && d[0]==3) || (len==64 && d[0]==4) || (len==72 && d[0]==5))) { out.valid=false; return false; }
    MonitorData m;
    m.rpm=word(d+1); m.kpa=word(d+3); m.tps=word(d+5)*0.1f;
    m.clt=int(d[7])-40; m.iat=int(d[8])-40; m.battery=d[9]*0.1f;
    m.measuredAfr=d[10]*0.1f; m.oxygenMv=d[11]*5; m.targetAfr=d[12]*0.1f;
    m.trimPercent=word(d+13)*100.0f/1024.0f-100.0f; m.ve=word(d+15);
    m.pulseUs=word(d+17); m.advance=int16_t(word(d+19))*0.1f;
    m.warmup=d[21]; m.afterstart=d[22]; m.accel=d[23]; m.iacPosition=d[24];
    m.idleTarget=d[25]*10; m.speed=d[26]; m.cellRpm=d[27]; m.rpmFraction=d[28]/256.0f;
    m.cellLoad=d[29]; m.loadFraction=d[30]/256.0f;
    m.state=d[31]; m.flags=d[32]; m.narrowbandBand=d[33]&15; m.gear=d[33]>>4;
    m.inhibits=word(d+34); m.generation=word(d+36); m.lossOfSyncCount=word(d+38);
    if(len>=44) {
        m.knockAvailable=true; m.knockMv=word(d+40);
        m.knockFlags=d[42]; m.knockRetard=d[43]*0.75f;
    }
    if(len>=64) {
        m.detailAvailable=true; m.iacTarget=d[44]; m.iacState=d[45]; m.iacFault=d[46];
        m.iacLastFault=d[47]; m.iacRetries=d[48]; m.iacFaultCount=d[49]; m.iacResponse=d[50];
        m.idleMode=d[51]; m.idleIntegral=int16_t(word(d+52))*0.1f; m.idleSpark=int16_t(word(d+54))*0.1f;
        m.syncLossReason=d[56]; m.syncLossTooth=d[57]; m.firstInhibit=word(d+58);
        m.captureResyncs=word(d+60); m.captureOverruns=word(d+62);
    }
    if(len==72) {
        m.outputDetailAvailable=true; m.tripSource=d[64]; m.latchClears=d[65];
        m.coilTimeouts=word(d+66); m.injectorClamps=word(d+68); m.lateEvents=word(d+70);
    }
    if(m.cellRpm>14 || m.cellLoad>14 || m.tps>100) { out.valid=false; return false; }
    m.valid=true; out=m; return true;
}
bool EcuProtocol::parseMonitorResponse(const unsigned char* d,int len) {
    return DecodeMonitor(d,len,monitorParseTarget ? *monitorParseTarget : monitor);
}
bool EcuProtocol::readMonitor(MonitorData& out) {
    unsigned char b[72];
    if(!liveV5Unsupported && !liveV4Unsupported) {
        const unsigned char request[]={CMD_MONITOR,5};
        const int n=transact(request,2,b,sizeof(b));
        if(n==72) {
            if(DecodeMonitor(b,n,out)) return true;
            lastFailure=LINK_DECODE; return false;
        }
        // Firmware 0.0.7 and older answer an unknown request with one status byte.
        if(n!=1) { out.valid=false; if(n>=0) lastFailure=LINK_DECODE; return false; }
        liveV5Unsupported=true;
    }
    if(!liveV4Unsupported) {
        const unsigned char request[]={CMD_MONITOR,4};
        const int n=transact(request,2,b,sizeof(b));
        if(n==64) {
            if(DecodeMonitor(b,n,out)) return true;
            lastFailure=LINK_DECODE; return false;
        }
        // Firmware 0.0.2 and older answer an unknown request with one status byte.
        if(n!=1) { out.valid=false; if(n>=0) lastFailure=LINK_DECODE; return false; }
        liveV4Unsupported=true;
    }
    const unsigned char cmd=CMD_MONITOR; const int n=transact(&cmd,1,b,44);
    if(DecodeMonitor(b,n,out)) return true;
    if(n>=0) lastFailure=LINK_DECODE;
    return false;
}
const char* IacStateName(int state) {
    switch(state) {
    case 0: return "Not referenced"; case 1: return "Homing"; case 2: return "Ready"; case 3: return "Fault";
    default: return "Unknown";
    }
}
const char* IacFaultName(int fault) {
    switch(fault) {
    case 0: return "None"; case 1: return "SPI transfer"; case 2: return "Open load";
    case 3: return "Driver diagnosis 10b"; case 4: return "Driver diagnosis 00b"; case 5: return "Homing timeout";
    case 6: return "Bridges not switched off"; case 7: return "Hold timing";
    default: return "Unknown";
    }
}
const char* IdleModeName(int mode) {
    switch(mode) {
    case 0: return "Unavailable"; case 1: return "Cranking"; case 2: return "Start catch"; case 3: return "Feedback";
    case 4: return "Overrun return"; case 5: return "Throttle open";
    default: return "Unknown";
    }
}
const char* SyncLossReasonName(int reason) {
    switch(reason) {
    case 0: return "None"; case 1: return "Gap too early"; case 2: return "Gap missing"; case 3: return "Irregular tooth";
    case 4: return "Noise edge"; case 5: return "Capture missed edge"; case 6: return "Capture fault";
    default: return "Unknown";
    }
}
const char* TripSourceName(int source) {
    switch(source) {
    case 0: return "None"; case 1: return "Coil driver refused a charge"; case 2: return "Injector driver refused a pulse";
    case 3: return "Coil watchdog tripped 3 times in a row"; case 4: return "Millisecond tick serviced late (> 3 ms)";
    case 5: return "Main loop gap (> 50 ms)";
    default: return "Unknown";
    }
}
const char* InhibitName(int bit) {
    static const char* names[]={"Crank sync","Calibration","Sensor","Stale plan","Service","Deadline","Key off / power","Board","Output driver"};
    return bit>=0 && bit<9 ? names[bit] : "Unknown";
}
const char* InhibitDetail(int bit) {
    switch(bit) {
    case 0: return "No valid crank angle. Fuel and spark stop until the trigger wheel gap has been seen twice in a row.\n"
                   "Normal with the engine stopped. While running it means a sync loss (see the last sync-loss cause).\n"
                   "Clears by itself when sync returns.";
    case 1: return "The ECU has no valid calibration. Write a valid tune and save it to ECU flash.";
    case 2: return "A required sensor is invalid. Check the sensor faults in Diagnostics.\nClears when the sensor reads valid again.";
    case 3: return "The fuel/spark plan is older than the plan age limit: the main loop did not publish a new plan in time.\n"
                   "Clears on the next control pass.";
    case 4: return "A tool session is running or ran: tune save, knock bench test or a refused firmware update.\n"
                   "Clears by itself when the session ends with the engine stopped; after any crank rotation it needs a key-off/on.";
    case 5: return "Timing deadline: the millisecond tick or the main loop was serviced late (see trip source).\n"
                   "Firmware 0.0.8: clears on the next control pass and costs at most a few events.\n"
                   "Firmware 0.0.7 and older: latched until ECU reset.";
    case 6: return "Ignition key input is off or battery power is too low. Clears when the key is on.";
    case 7: return "This firmware image does not release the output drivers (development build).\n"
                   "Flash the release firmware.";
    case 8: return "Output driver latch: a driver refused a start, or one coil's 6.5 ms watchdog tripped 3 times in a row\n"
                   "while running (stuck driver or overheating coil). See the trip source.\n"
                   "Firmware 0.0.8: clears by itself after the engine has stopped for 1 s with all outputs off,\n"
                   "at most 3 times per key cycle; after that a key-off/on is needed. Older firmware: latched until ECU reset.";
    default: return "Unknown inhibit bit.";
    }
}
bool InhibitLatches(int bit, bool before008) {
    return bit==4 || bit==7 || bit==8 || (before008 && bit==5);
}
bool DecodeTimingHealth(const unsigned char* d,int len,TimingHealth& out) {
    if(!d || len!=96 || d[0]!=2) return false;
    TimingHealth h;
    h.passMaxMs=word(d+2); h.intervalMaxMs=word(d+4); h.tickMaxMs=word(d+6); h.planAgeMaxMs=word(d+8);
    h.captureOverruns=word(d+10); h.lateEvents=word(d+12);
    h.captureReason=word(d+14); h.counter=word(d+16); h.expected=word(d+18); h.last=word(d+20);
    h.latest=word(d+22); h.irq=word(d+24); h.pec=word(d+26); h.dest=word(d+28);
    h.head=d[30]; h.tail=d[31]; h.next=d[32]; h.count=d[33]; h.resyncs=word(d+34);
    if(h.count>30) return false;
    for(int i=0;i<30;++i) h.words[i]=word(d+36+2*i);
    out=h; return true;
}
const char* CaptureFailureName(int reason) {
    switch(reason) {
    case 0: return "None since ECU reset"; case 1: return "Worker starved (capture queue full)";
    case 2: return "Counted edge without a capture (missed edge)"; case 4: return "Acquisition seed failed";
    default: return "Unknown";
    }
}
std::vector<int> LostCaptureEdges(const TimingHealth& h,uint16_t* normalOut) {
    std::vector<int> found;
    std::vector<uint16_t> intervals;
    for(int i=1;i<h.count;++i) intervals.push_back(uint16_t(h.words[i]-h.words[i-1]));
    if(normalOut) *normalOut=0;
    if(intervals.size()<3) return found;
    std::vector<uint16_t> sorted=intervals; std::sort(sorted.begin(),sorted.end());
    const unsigned normal=sorted[sorted.size()/2];
    if(normalOut) *normalOut=uint16_t(normal);
    for(size_t i=0;i<intervals.size();++i)
        if(normal && intervals[i]*10u>=normal*16u && intervals[i]*10u<normal*25u) found.push_back(int(i)+1);
    return found;
}
static uint32_t dword(const unsigned char* d) { return uint32_t(word(d))<<16 | word(d+2); }
bool DecodeKnockDetail(const unsigned char* d,int len,KnockDetail& out) {
    if(!d || (len!=64 && len!=86) || d[0]!=1 || (len==86 && d[64]!=1)) return false;
    KnockDetail k;
    k.mv=word(d+1); k.flags=d[3]; k.retard=d[4];
    k.raw=word(d+5); k.sequence=word(d+7);
    k.stampMs=dword(d+9); k.sampleAgeMs=dword(d+13); k.count=dword(d+17); k.lastEventAgeMs=dword(d+21);
    k.source=d[25]; k.type=d[26]; k.offset=d[27]; k.amplitude=d[28]; k.reference=d[29]; k.gain=d[30];
    k.gainCode=d[31]; k.ratio=d[32]; k.threshold=d[33]; k.decision=d[34]; k.scheduled=d[35]; k.fault=d[36];
    k.hold=word(d+37); k.epoch=word(d+39); k.generation=word(d+41);
    k.adcTimeouts=word(d+43); k.missedWindows=word(d+45); k.staleSamples=word(d+47); k.ceilingEvents=word(d+49);
    k.normalCount=dword(d+51); k.nullCount=dword(d+55); k.testCount=dword(d+59); k.nullStart=d[63];
    if(len==86) {
        k.job=true; k.jobState=d[65]; k.jobCompleted=d[66]; k.jobFault=d[67];
        k.nullRaw=word(d+68); k.testRaw=word(d+70);
        for(int i=0;i<7;++i) k.gainRaw[i]=word(d+72+2*i);
    }
    out=k; return true;
}
const char* KnockFaultName(int fault) {
    switch(fault) {
    case 0: return "None";
    case 1: return "ADC conversion";
    case 2: return "Stale sample";
    case 3: return "Measurement window";
    case 4: return "Reference";
    case 5: return "Null level";
    case 6: return "Test pulse";
    case 7: return "Signal at rail";
    case 8: return "Null drift";
    default: return "Unknown";
    }
}
const char* KnockFaultDetail(int fault) {
    switch(fault) {
    case 0: return "No knock sensing fault.";
    case 1: return "The knock ADC conversion (AN15) did not complete in its window.\n"
                   "Points to the ECU's knock input or ADC, not the sensor wiring.";
    case 2: return "No fresh knock sample arrived within the stale time while knock sensing was active,\n"
                   "or the tune or crank sync changed during a sample. Check crank sync and the stale time setting.";
    case 3: return "A knock measurement window could not be opened or closed on time (ECU timing).\n"
                   "Check the Developer tab timing health; very high rpm or a late window start can cause it.";
    case 4: return "The adapted knock reference or the reference divisor is zero, so no knock ratio can be formed.\n"
                   "Check the knock calibration.";
    case 5: return "The null measurement (knock IC input shorted internally) read outside 37 +/- the null tolerance\n"
                   "several times in a row. Points to the knock IC or its supply.";
    case 6: return "The knock IC's internal test pulse did not raise the reading by the expected amount\n"
                   "several times in a row. Points to the knock IC input path.";
    case 7: return "The knock signal read 0 or full scale several samples in a row.\n"
                   "Typical of an open or shorted sensor circuit: check the sensor, connector and wiring.";
    case 8: return "The null level drifted faster than the OEM limit above 2000 rpm (or on the bench).\n"
                   "Points to a noisy or unstable knock input.";
    default: return "Unknown knock fault code.";
    }
}
const char* KnockJobStateName(int state) {
    switch(state) {
    case 0: return "Idle";
    case 1: return "Running";
    case 2: return "Finished";
    case 3: return "Finished with a fault";
    case 4: return "Cancelled";
    default: return "Unknown";
    }
}
bool EcuProtocol::knockDetail(KnockDetail& out,bool withJob) {
    unsigned char b[96];
    const unsigned char detail[]={0x34}, job[]={0x35,3};
    const int want=withJob?86:64;
    return transact(withJob?job:detail,withJob?2:1,b,sizeof(b))==want && DecodeKnockDetail(b,want,out);
}
bool EcuProtocol::knockJob(uint8_t action) {
    const unsigned char request[]={0x35,action}; unsigned char r[1];
    return transact(request,2,r,1,1200)==1 && r[0]==0;
}
bool EcuProtocol::timingHealth(TimingHealth& out,bool clearMaxima) {
    unsigned char b[96];
    const unsigned char request[]={0x2F,2};
    if(transact(request,2,b,sizeof(b))!=96 || !DecodeTimingHealth(b,96,out)) return false;
    if(clearMaxima) {
        const unsigned char clear[]={0x2F,1}; unsigned char r[14];
        if(transact(clear,2,r,sizeof(r))!=14) return false;
    }
    return true;
}
void EcuProtocol::pollMonitor() {
    if(readMonitor(monitor)) monitorFailCount=0; else ++monitorFailCount;
}
// One live-frame poll. The frame is stamped when its reply is complete and
// queued; the UI drains the queue, so no frame is lost while a later request
// is in flight, and log times reflect when the ECU answered.
void EcuProtocol::monitorCycle() {
    EnterCriticalSection(&serialCS);
    const bool ok=readMonitor(threadMonitor);
    const uint8_t cause=ok ? LINK_OK : (lastFailure ? lastFailure : LINK_DECODE);
    LARGE_INTEGER stamp; QueryPerformanceCounter(&stamp);
    MonitorSample sample;
    if(ok) sample.data=threadMonitor;
    LeaveCriticalSection(&serialCS);
    if(!ok) {
        ++monitorFailCount; ++linkFailureTotal; linkLastFailure=cause;
        return;
    }
    monitorFailCount=0;
    sample.data.linkFailures=linkFailureTotal;
    sample.data.linkLastFailure=linkLastFailure;
    sample.receivedQpc=stamp.QuadPart;
    std::lock_guard<std::mutex> guard(sampleMutex);
    if(sampleQueue.size()>=MONITOR_QUEUE_MAX) { sampleQueue.pop_front(); ++droppedTotal; }
    sample.data.droppedFrames=droppedTotal;
    sampleQueue.push_back(sample);
}
static DWORD WINAPI MonitorThreadProc(LPVOID param) {
    EcuProtocol* ecu = (EcuProtocol*)param;
    // Keep polling on time while the window is in the background.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    while (ecu->threadRunning) {
        if (ecu->threadPaused || !ecu->monitorActive || !ecu->port || !ecu->port->isOpen()) {
            Sleep(10);
            continue;
        }
        ecu->monitorCycle();
        Sleep(ecu->monitorIntervalMs);
    }
    return 0;
}

void EcuProtocol::initThread() {
    InitializeCriticalSection(&serialCS);
}

void EcuProtocol::startMonitor() {
    if (monitorThread) return; // already running
    threadRunning = true;
    threadPaused = false;
    monitorActive = true;
    monitorThread = CreateThread(nullptr, 0, MonitorThreadProc, this, 0, nullptr);
}

bool EcuProtocol::stopMonitor() {
    if (!monitorThread) return true;
    threadRunning = false;
    const DWORD waitResult = WaitForSingleObject(monitorThread, 10000);
    if (waitResult != WAIT_OBJECT_0) return false;
    CloseHandle(monitorThread);
    monitorThread = nullptr;
    monitorActive = false;
    return true;
}

void EcuProtocol::destroyThread() {
    stopMonitor();
    DeleteCriticalSection(&serialCS);
}

bool EcuProtocol::consumeMonitorUpdate(MonitorData& out) {
    std::lock_guard<std::mutex> guard(sampleMutex);
    if (sampleQueue.empty()) return false;
    out = sampleQueue.back().data;
    sampleQueue.clear();
    return true;
}

size_t EcuProtocol::drainMonitorSamples(std::vector<MonitorSample>& out) {
    out.clear();
    std::lock_guard<std::mutex> guard(sampleMutex);
    out.assign(sampleQueue.begin(), sampleQueue.end());
    sampleQueue.clear();
    return out.size();
}

int EcuProtocol::getMonitorFailCount() {
    return monitorFailCount;
}

void EcuProtocol::resetMonitorFailCount() {
    monitorFailCount = 0;
}

void EcuProtocol::clearMonitorUpdates() {
    EnterCriticalSection(&serialCS);
    threadMonitor = {};
    monitor.valid = false;
    linkFailureTotal = 0;
    linkLastFailure = LINK_OK;
    LeaveCriticalSection(&serialCS);
    std::lock_guard<std::mutex> guard(sampleMutex);
    sampleQueue.clear();
    droppedTotal = 0;
}
