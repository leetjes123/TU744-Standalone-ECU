#include "calibration.h"
#include "calibration_safety.h"
#include "protocol.h"
#include "protocol_codec.h"
#include "tune_transfer.h"
#include "firmware_flash.h"
#include "autotune.h"
#include "diagnostics.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
using Reset=void(*)();
using Bridge=unsigned short(*)(const unsigned char*,unsigned short,unsigned char*);
using SetLoss=void(*)(uint32_t);
static Reset reset;
static Bridge bridge;
static SetLoss loss;
static int actual(const unsigned char* data,int n,unsigned char* out,int cap,int) {
    unsigned char packet[256],reply[256];
    const int size=BuildRequestPacket(data,n,packet,256); CHECK(size>0);
    const int length=bridge(packet,(unsigned short)size,reply);
    return ParseResponsePacket(reply,length,out,cap);
}
static void finish(TuneTransfer& job,EcuProtocol& ecu) {
    for(int i=0;i<500 && job.busy();++i) job.step(ecu);
    CHECK(!job.busy());
}
static bool firmwareAccepts(EcuProtocol& ecu,const CalBuffer& c) {
    reset(); CHECK(ecu.command(0x21));
    for(int i=0;i<CAL_SIZE;i+=32) CHECK(ecu.writeChunk(i,32,c.data+i));
    bool result=ecu.command(0x22); ecu.command(0x23); return result;
}
void DiagnosticTests();
int main() {
    DiagnosticTests();
    HMODULE lib=LoadLibraryA(TEST_DLL_PATH); CHECK(lib);
    reset=(Reset)GetProcAddress(lib,"bridge_reset"); bridge=(Bridge)GetProcAddress(lib,"bridge_exchange");
    loss=(SetLoss)GetProcAddress(lib,"bridge_sync_losses"); CHECK(reset&&bridge&&loss);
    EcuProtocol ecu; ecu.exchange=actual; reset();
    // Actual firmware rejects unavailable history instead of reporting zero faults.
    Diagnostics unavailable; unavailable.start(false,0); unavailable.step(ecu,0);
    CHECK(unavailable.phase==Diagnostics::Phase::Failed && !unavailable.snapshot.valid);
    CHECK(!ecu.command(0x30));
    TuneTransfer read; CHECK(read.read(ecu)); finish(read,ecu); CHECK(read.phase==TuneTransfer::Phase::Complete);
    CalBuffer original; memcpy(original.data,read.image.data(),CAL_SIZE); original.loaded=true; original.markClean();
    CHECK(ValidateCalibration(original).empty()); CHECK(HasSchemaMarker(original.data));
    CHECK(original.loadFromFile(TEST_TUNE_PATH));
    // Generated registry ranges and overlapping editable definitions.
    unsigned char used[CAL_SIZE]={};
    auto occupy=[&](int at,int size,int mask) {
        CHECK(at>=0 && size>0 && at+size<=CAL_SIZE);
        for(int i=at;i<at+size;++i) { if(used[i]&mask) fprintf(stderr,"Overlap at %03X used %02X mask %02X\n",i,used[i],mask); CHECK(!(used[i]&mask)); used[i]|=(unsigned char)mask; }
    };
    for(int i=0;i<NUM_TABLES;++i) {
        const auto& t=ALL_TABLES[i]; occupy(t.offset,t.rows*t.cols*original.cellSize(t.cellType),255);
        CHECK(t.description && *t.description);
        for(auto a:{t.xAxis,t.yAxis}) if(a) {
            const auto& v=ResolveAxis(*a,original.data);
            if(v.offset>=0) CHECK(v.offset+v.count*original.cellSize(v.type)<=CAL_SIZE);
            else CHECK(v.fixed);
        }
    }
    for(int i=0;i<NUM_SCALARS;++i) occupy(ALL_SCALARS[i].offset,original.cellSize(ALL_SCALARS[i].type),255);
    for(int i=0;i<NUM_FLAGS;++i) occupy(ALL_FLAGS[i].offset,1,ALL_FLAGS[i].mask);
    for(int i=0;i<NUM_DROPDOWNS;++i) occupy(ALL_DROPDOWNS[i].offset,1,ALL_DROPDOWNS[i].mask);
    CalBuffer c=original;
    CHECK(&ResolveAxis(AXIS_LOAD,c.data)==&AXIS_KPA); c.data[0x5D4]|=1;
    CHECK(&ResolveAxis(AXIS_LOAD,c.data)==&AXIS_TPS);
    CHECK(c.readAxisValue(AXIS_MAP_ADC,15)==1023); CHECK(c.readAxisValue(AXIS_VOLTAGE,0)==4);
    c.writeAxisValue(AXIS_LOAD,1,3); CHECK(c.readU16BE(0x442)==3); CHECK(c.readU16BE(0x422)==40);
    // Compare generated all-issues validator with the actual C parser.
    const int targeted[]={0x100,0x200,0x300,0x400,0x460,0x490,0x5D4,0x5D8,0x5D9,0x5DB,0x600,0x605,0x630,0x730,0x740,0x750,0x758,0x7B0,0x7B4,0x8CF,0x900,0x910,0x918,0x926,0x93A,0x93E,0x93F,0x940,0x960,0x980};
    int checks=0;
    for(int at:targeted) for(unsigned char value:{(unsigned char)0,(unsigned char)127,(unsigned char)255}) {
        c=original; c.data[at]=value;
        CHECK(ValidateCalibration(c).empty()==firmwareAccepts(ecu,c)); ++checks;
    }
    uint32_t rng=0x71234;
    for(int i=0;i<256;++i) {
        rng=rng*1664525+1013904223; c=original; int at=(rng>>8)%CAL_SIZE;
        c.data[at]^=(unsigned char)rng;
        CHECK(ValidateCalibration(c).empty()==firmwareAccepts(ecu,c)); ++checks;
    }
    c=original; c.data[0x100]=255; c.data[0x200]=0; CHECK(ValidateCalibration(c).size()>=2);
    // Strict compact decoding, signed units, no identity field or legacy alias.
    reset(); MonitorData m;
    for(uint32_t v:{0u,1u,255u,256u,0x1234u,65535u,65536u,0xffffffffu}) {
        loss(v); CHECK(ecu.readMonitor(m)); CHECK(m.lossOfSyncCount==std::min(v,65535u));
    }
    unsigned char frame[40]={}; frame[0]=2; frame[7]=190; frame[8]=0; frame[19]=0xff; frame[20]=0x9c;
    frame[5]=3; frame[6]=0xe7; frame[15]=3; frame[16]=0xe8; frame[38]=0x12; frame[39]=0x34;
    CHECK(DecodeMonitor(frame,40,m)); CHECK(m.clt==150 && m.iat==-40 && m.ve==1000);
    CHECK(!m.knockAvailable);
    CHECK(std::abs(m.advance+10)<.001 && std::abs(m.tps-99.9f)<.001 && m.lossOfSyncCount==0x1234);
    CHECK(!DecodeMonitor(frame,39,m) && !m.valid); frame[0]=1; CHECK(!DecodeMonitor(frame,40,m)); frame[0]=3; CHECK(!DecodeMonitor(frame,40,m));
    frame[0]=2; frame[27]=15; CHECK(!DecodeMonitor(frame,40,m));
    unsigned char knockFrame[44]={}; knockFrame[0]=3;
    knockFrame[40]=0x08; knockFrame[41]=0xed; knockFrame[42]=0xc7; knockFrame[43]=4;
    CHECK(DecodeMonitor(knockFrame,44,m) && m.knockAvailable);
    CHECK(m.knockMv==2285 && m.knockFlags==0xc7 && std::abs(m.knockRetard-3)<.001);
    CHECK(!DecodeMonitor(knockFrame,43,m));
    unsigned char out[128],cmd[]={0x13},packet[10]; CHECK(BuildRequestPacket(cmd,1,packet,10)==4);
    CHECK(packet[0]==0xaa && packet[3]==0x14);
    unsigned char response[]={0x55,1,0x12,0x13}; CHECK(ParseResponsePacket(response,4,out,128)==1); response[3]^=1; CHECK(ParseResponsePacket(response,4,out,128)==-1);
    // Live writes cannot cross a map boundary or exceed 16 bytes.
    auto runs=SplitLiveWrites({{0xf8,0x122},{0x2ff,0x301}}); int total=0;
    for(auto r:runs) { CHECK(r.end-r.start<=16); CHECK(r.start/256==(r.end-1)/256); total+=r.end-r.start; }
    CHECK(total==44); CHECK(SplitLiveWrites({{0x3ff,0x401}}).empty());
    uint16_t liveGeneration=0,liveEdits=0;
    CHECK(!ecu.liveWrite(255,2,original.data,liveGeneration,liveEdits));
    // End-to-end write flows against the C firmware, plus ACK loss and rejection.
    reset(); c=original; c.data[0xfe]++; c.data[0xff]++; c.data[0x100]++;
    int liveReadBytes=0;
    ecu.exchange=[&](const unsigned char* p,int n,unsigned char* o,int cap,int timeout) {
        if(p[0]==4) liveReadBytes+=p[3]; return actual(p,n,o,cap,timeout);
    };
    TuneTransfer write; CHECK(write.write(ecu,c,original.data,true)); finish(write,ecu); CHECK(write.phase==TuneTransfer::Phase::Complete);
    CHECK(liveReadBytes==3); ecu.exchange=actual;
    Capabilities caps; CHECK(ecu.capabilities(caps) && caps.generation==1);
    reset(); c=original; c.data[0x5E5]++;
    CHECK(write.write(ecu,c,original.data,true)); finish(write,ecu); CHECK(write.phase==TuneTransfer::Phase::Complete);
    CHECK(ecu.capabilities(caps) && caps.generation==2);
    reset(); c=original; c.data[0x100]=255; CHECK(!write.write(ecu,c,original.data,true));
    CHECK(!write.write(ecu,original,original.data,false));
    reset(); c=original; c.data[0x5E5]++;
    ecu.exchange=[](const unsigned char* p,int n,unsigned char* o,int cap,int timeout) {
        int result=actual(p,n,o,cap,timeout); return p[0]==0x22?-1:result;
    };
    CHECK(write.write(ecu,c,original.data,true)); finish(write,ecu); CHECK(write.phase==TuneTransfer::Phase::Failed);
    ecu.exchange=actual; CHECK(ecu.capabilities(caps) && caps.generation==2);
    reset(); ecu.exchange=[](const unsigned char* p,int n,unsigned char* o,int cap,int timeout) {
        if(p[0]==5) { o[0]=1; return 1; } return actual(p,n,o,cap,timeout);
    };
    CHECK(write.write(ecu,c,original.data,true)); finish(write,ecu); CHECK(write.phase==TuneTransfer::Phase::Failed);
    ecu.exchange=actual; CHECK(ecu.command(0x21)); CHECK(ecu.command(0x23));
    // Unknown calibration can be compared for a full restore without loading it
    // into the editor as a valid tune; a normal Read still rejects its marker.
    reset(); ecu.exchange=[](const unsigned char* p,int n,unsigned char* o,int cap,int timeout) {
        if(p[0]==4) { memset(o,0xff,p[3]); return int(p[3]); } return actual(p,n,o,cap,timeout);
    };
    TuneTransfer empty;
    CHECK(empty.read(ecu)); finish(empty,ecu); CHECK(empty.phase==TuneTransfer::Phase::Failed);
    CHECK(empty.read(ecu,true)); finish(empty,ecu); CHECK(empty.phase==TuneTransfer::Phase::Complete);
    ecu.exchange=actual; CHECK(write.write(ecu,original,empty.image.data(),true)); finish(write,ecu);
    CHECK(write.phase==TuneTransfer::Phase::Complete);
    // A fresh running report blocks structural writes before a transaction.
    reset(); c=original; c.data[0x5D4]^=1;
    ecu.exchange=[](const unsigned char* p,int n,unsigned char* o,int cap,int timeout) {
        int result=actual(p,n,o,cap,timeout); if(p[0]==0x13 && result>=40) { o[1]=3; o[2]=0xe8; o[31]=5; } return result;
    };
    CHECK(!write.write(ecu,c,original.data,true)); ecu.exchange=actual;
    MonitorData duty; duty.rpm=6000; duty.pulseUs=5000; CHECK(duty.plannedDutyPercent()==50);
    // Save polls result byte 8 and does not confuse acceptance with completion.
    int saved=0;
    ecu.exchange=[&](const unsigned char* p,int n,unsigned char* o,int cap,int timeout) {
        if(p[0]==0x24) { saved=1; o[0]=0; return 1; }
        if(p[0]==0x25) { memset(o,0,34); o[8]=saved++==1?0:1; o[9]=1; return 34; }
        return actual(p,n,o,cap,timeout);
    };
    TuneTransfer save; CHECK(save.save(ecu)); CHECK(save.busy()); save.step(ecu); CHECK(save.busy()); save.step(ecu); CHECK(save.phase==TuneTransfer::Phase::Complete);
    // Autotune has no data at ineligible states, and bounds proposals.
    AutoTune tune; tune.collecting=true; tune.delaySeconds=0.1f;
    m={}; m.valid=true; m.state=5; m.warmup=m.afterstart=m.accel=100;
    m.rpm=2500; m.targetAfr=14; m.measuredAfr=15; m.cellRpm=2; m.cellLoad=3;
    for(int i=0;i<100;++i) tune.sample(m,i*.05f,true);
    CalBuffer proposed; CHECK(tune.propose(original,proposed)==1); CHECK(proposed.data[50]==61);
    tune.clear(); m.state|=16; for(int i=0;i<100;++i) tune.sample(m,i*.05f,true);
    CHECK(tune.propose(original,proposed)==0);
    // File marker rejection preserves the existing local buffer.
    FILE* f=fopen("invalid-tune.bin","wb"); CHECK(f); unsigned char bad[CAL_SIZE]={}; fwrite(bad,1,CAL_SIZE,f); fclose(f);
    c=original; CHECK(!c.loadFromFile("invalid-tune.bin")); CHECK(!memcmp(c.data,original.data,CAL_SIZE)); remove("invalid-tune.bin");
    CHECK(c.saveToFile("roundtrip.bin")); CalBuffer loaded; CHECK(loaded.loadFromFile("roundtrip.bin")); CHECK(!memcmp(c.data,loaded.data,CAL_SIZE)); remove("roundtrip.bin");
    // A minimal accepted full image must still erase both tune slots.
    std::vector<unsigned char> image(0x80000,255); std::fill(image.begin(),image.begin()+2048,0); image[0]=0xfa;
    f=fopen("flash-test.bin","wb"); CHECK(f); fwrite(image.data(),1,image.size(),f); fclose(f);
    FirmwareImageInfo info; std::string error; CHECK(LoadAndValidateFirmwareImage("flash-test.bin",image,info,error)); CHECK(info.sectorsToErase==10); remove("flash-test.bin");
    printf("PASS: schema, %d differential validator cases, compact frames, transfers, recovery, save, autotune, files and flash scope\n",checks);
    FreeLibrary(lib); return 0;
}
