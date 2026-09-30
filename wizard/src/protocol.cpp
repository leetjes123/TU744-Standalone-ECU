#include "protocol.h"
#include "protocol_codec.h"
#include <cstdio>
#include <cstring>
#include <windows.h>

// ============================================================
//  Core transact: send packet, read echo+response, parse
//  This matches the proven Python approach: read ALL bytes
//  (echo + response) as one blob, skip the echo by known
//  packet length, then parse the response portion.
// ============================================================

int EcuProtocol::transact(const unsigned char* payload,int payloadLen,unsigned char* response,int capacity,int timeoutMs) {
    if(exchange) return exchange(payload,payloadLen,response,capacity,timeoutMs);
    if(!port || !port->isOpen()) return -1;
    unsigned char packet[128],raw[512];
    const int length=BuildRequestPacket(payload,payloadLen,packet,sizeof(packet));
    if(length<0) return -1;
    PurgeComm(port->handle,PURGE_RXCLEAR);
    DWORD written=0;
    if(!WriteFile(port->handle,packet,length,&written,nullptr) || written!=DWORD(length)) return -1;
    COMMTIMEOUTS timeouts={}; timeouts.ReadIntervalTimeout=MAXDWORD;
    timeouts.ReadTotalTimeoutConstant=1; timeouts.WriteTotalTimeoutConstant=500;
    if(!SetCommTimeouts(port->handle,&timeouts)) return -1;
    int total=0; const DWORD start=GetTickCount();
    while(GetTickCount()-start<DWORD(timeoutMs) && total<int(sizeof(raw))) {
        COMSTAT state={}; DWORD errors=0;
        if(!ClearCommError(port->handle,&errors,&state) || errors) return -1;
        if(!state.cbInQue) { Sleep(1); continue; }
        DWORD count=0;
        const DWORD want=state.cbInQue<DWORD(sizeof(raw)-total)?state.cbInQue:DWORD(sizeof(raw)-total);
        if(!ReadFile(port->handle,raw+total,want,&count,nullptr)) return -1;
        total+=int(count);
        // Accept adapters with or without local echo, never arbitrary payload
        // bytes that happen to resemble a frame header.
        int at=0;
        if(total && raw[0]==REQ_HEADER) {
            if(total<length) continue;
            if(memcmp(raw,packet,length)) return -1;
            at=length;
        }
        if(total>=at+2) {
            if(raw[at]!=RESP_HEADER) return -1;
            if(total>=at+int(raw[at+1])+3)
                return ParseResponsePacket(raw+at,total-at,response,capacity);
        }
    }
    return -1;
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
    if(!d || !((len==40 && d[0]==2) || (len==44 && d[0]==3))) { out.valid=false; return false; }
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
    if(len==44) {
        m.knockAvailable=true; m.knockMv=word(d+40);
        m.knockFlags=d[42]; m.knockRetard=d[43]*0.75f;
    }
    if(m.cellRpm>14 || m.cellLoad>14 || m.tps>100) { out.valid=false; return false; }
    m.valid=true; out=m; return true;
}
bool EcuProtocol::parseMonitorResponse(const unsigned char* d,int len) {
    return DecodeMonitor(d,len,monitorParseTarget ? *monitorParseTarget : monitor);
}
bool EcuProtocol::readMonitor(MonitorData& out) {
    unsigned char cmd=CMD_MONITOR,b[44]; return DecodeMonitor(b,transact(&cmd,1,b,44),out);
}
void EcuProtocol::pollMonitor() {
    if(readMonitor(monitor)) monitorFailCount=0; else ++monitorFailCount;
}
static DWORD WINAPI MonitorThreadProc(LPVOID param) {
    EcuProtocol* ecu = (EcuProtocol*)param;

    while (ecu->threadRunning) {
        if (ecu->threadPaused || !ecu->monitorActive || !ecu->port || !ecu->port->isOpen()) {
            Sleep(10);
            continue;
        }

        EnterCriticalSection(&ecu->serialCS);

        if (ecu->readMonitor(ecu->threadMonitor)) {
            ecu->threadNewData=true; ecu->monitorFailCount=0;
        } else { ++ecu->monitorFailCount; }
        LeaveCriticalSection(&ecu->serialCS);

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
    if (!TryEnterCriticalSection(&serialCS)) return false;
    const bool available = threadNewData;
    if (available) {
        out = threadMonitor;
        threadNewData = false;
    }
    LeaveCriticalSection(&serialCS);
    return available;
}

int EcuProtocol::getMonitorFailCount() {
    if (!TryEnterCriticalSection(&serialCS)) return 0;
    const int count = monitorFailCount;
    LeaveCriticalSection(&serialCS);
    return count;
}

void EcuProtocol::resetMonitorFailCount() {
    EnterCriticalSection(&serialCS);
    monitorFailCount = 0;
    LeaveCriticalSection(&serialCS);
}

void EcuProtocol::clearMonitorUpdates() {
    EnterCriticalSection(&serialCS);
    threadNewData = false;
    threadMonitor = {};
    monitor.valid = false;
    LeaveCriticalSection(&serialCS);
}
