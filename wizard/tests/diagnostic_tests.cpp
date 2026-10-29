#include "diagnostics.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"Diagnostic failure at %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)

// Live frames are queued with receive stamps; none are lost between UI frames,
// and failed requests are counted with their cause (Tuning Wizard 0.0.5).
static void MonitorQueueTests() {
    EcuProtocol ecu; ecu.initThread();
    int calls=0; int mode=0; // 0 valid frame, 1 no reply, 2 frame fails validation
    ecu.exchange=[&](const unsigned char* p,int,unsigned char* o,int cap,int) {
        REQUIRE(p[0]==CMD_MONITOR && cap>=72);
        if(mode==1) return -1;
        memset(o,0,72); o[0]=mode==2?9:5; ++calls;
        o[1]=(unsigned char)(calls>>8); o[2]=(unsigned char)calls; // rpm = call number
        return 72;
    };
    std::vector<MonitorSample> batch;
    ecu.monitorCycle(); mode=1; ecu.monitorCycle(); ecu.monitorCycle();
    mode=2; ecu.monitorCycle(); mode=0; ecu.monitorCycle(); ecu.monitorCycle();
    REQUIRE(ecu.getMonitorFailCount()==0);
    REQUIRE(ecu.drainMonitorSamples(batch)==3);
    REQUIRE(batch[0].data.rpm==1 && batch[1].data.rpm==3 && batch[2].data.rpm==4);
    REQUIRE(batch[0].data.linkFailures==0 && batch[0].data.linkLastFailure==LINK_OK);
    REQUIRE(batch[1].data.linkFailures==3 && batch[1].data.linkLastFailure==LINK_DECODE);
    REQUIRE(batch[0].receivedQpc<=batch[1].receivedQpc && batch[1].receivedQpc<=batch[2].receivedQpc);
    REQUIRE(ecu.drainMonitorSamples(batch)==0);
    mode=1; ecu.monitorCycle(); REQUIRE(ecu.getMonitorFailCount()==1);
    mode=0; ecu.monitorCycle(); ecu.drainMonitorSamples(batch);
    REQUIRE(batch.size()==1 && batch[0].data.linkFailures==4 && batch[0].data.linkLastFailure==LINK_TIMEOUT);
    // Bounded: a stalled UI keeps the newest frames.
    for(size_t i=0;i<EcuProtocol::MONITOR_QUEUE_MAX+10;++i) ecu.monitorCycle();
    REQUIRE(ecu.drainMonitorSamples(batch)==EcuProtocol::MONITOR_QUEUE_MAX);
    REQUIRE(batch.back().data.rpm==uint16_t(calls));
    // Each frame carries the drop count when it was queued: the overflow follows the oldest kept frame.
    REQUIRE(batch.front().data.droppedFrames==0 && batch.back().data.droppedFrames==10);
    // Reconnect clears queue and link counters.
    ecu.monitorCycle(); ecu.clearMonitorUpdates(); REQUIRE(ecu.drainMonitorSamples(batch)==0);
    mode=1; ecu.monitorCycle(); ecu.clearMonitorUpdates(); mode=0; ecu.monitorCycle();
    ecu.drainMonitorSamples(batch);
    REQUIRE(batch.size()==1 && batch[0].data.linkFailures==0 && batch[0].data.droppedFrames==0);
    MonitorData latest; ecu.monitorCycle(); ecu.monitorCycle();
    REQUIRE(ecu.consumeMonitorUpdate(latest) && latest.rpm==uint16_t(calls) && !ecu.consumeMonitorUpdate(latest));
    ecu.destroyThread();
}

void DiagnosticTests() {
    MonitorQueueTests();
    unsigned char raw[24]={}; raw[0]=0x43; raw[2]=1; raw[3]=0x21; raw[4]=24;
    raw[9]=126; raw[10]=67; raw[11]=80; raw[12]=92; raw[13]=139; raw[14]=100;
    raw[17]=74; raw[18]=2; raw[19]=128; raw[20]=0x12; raw[21]=0x34; raw[22]=7;
    DtcRecord r; REQUIRE(DecodeDtc(raw,24,r));
    REQUIRE(r.code=="P0108" && r.firstCode=="P0107" && r.active && r.mil && r.flashing);
    REQUIRE(r.rpm==3200 && r.coolant==86 && r.intake==27 && r.map==92 && r.throttle==32);
    REQUIRE(r.trim==100 && r.trimAvailable && r.clock==0x1234 && r.occurrences==7);
    raw[19]=255; REQUIRE(DecodeDtc(raw,24,r) && !r.trimAvailable);
    raw[3]=0x30; REQUIRE(DecodeDtc(raw,24,r) && r.code=="Event 43 / 0" && r.firstCode=="Event 43 / 3");
    REQUIRE(!DecodeDtc(raw,23,r)); raw[0]=106; REQUIRE(!DecodeDtc(raw,24,r)); raw[0]=0x43; raw[3]=0x21;

    EcuProtocol ecu; Diagnostics job; unsigned count=1; int reads=0,clears=0;
    bool stopped=true; int clearReply=0; bool mutate=false; int bad=0;
    ecu.exchange=[&](const unsigned char* p,int,unsigned char* o,int cap,int) {
        memset(o,0,cap);
        switch(p[0]) {
        case 0x31: o[0]=1; o[1]=stopped?2:0; return 12;
        case 0x30: ++clears; o[0]=(unsigned char)clearReply; return clearReply<0?-1:1;
        case 0x27:
            o[0]=1; o[7]=o[8]=o[14]=1; o[20]=(unsigned char)count;
            o[18]=clears!=0; o[15]=clears!=0; o[21]=1;
            if(bad==1) return 23; if(bad==2) o[0]=2; if(bad==3) o[14]=0;
            return 24;
        case 0x29:
            memcpy(o,raw,24); o[0]=(unsigned char)(p[1]+3); ++reads;
            if(mutate) o[22]+= (unsigned char)reads;
            return bad==4?23:24;
        case 0x2B: o[0]=1; o[1]=6; o[3]=o[5]=1; o[6]=1; return bad==5?11:12;
        case 0x2C: o[0]=1; o[1]=p[1]; o[2]=o[3]=1; o[4]=2; o[6]=1; o[7]=2; o[8]=1; o[15]=50; return 16;
        }
        return -1;
    };
    auto finish=[&]() { for(uint64_t t=0;t<21000 && job.busy();t+=50) job.step(ecu,t); REQUIRE(!job.busy()); };
    job.start(false,0); finish(); REQUIRE(job.phase==Diagnostics::Phase::Complete);
    REQUIRE(job.snapshot.valid && job.snapshot.records.size()==1 && reads==2);
    REQUIRE(job.snapshot.controller.size()==1 && job.snapshot.controller[0].occurrences==258 && job.snapshot.controller[0].first==0x1000000);
    for(bad=1;bad<=5;++bad) {
        job.start(false,0); finish(); REQUIRE(job.phase==Diagnostics::Phase::Failed);
        REQUIRE(job.snapshot.valid && job.snapshot.records.size()==1); // Never replace with an empty failure.
    }
    bad=0; count=21; job.start(false,0); finish(); REQUIRE(job.phase==Diagnostics::Phase::Failed);
    count=20; job.start(false,0); finish(); REQUIRE(job.phase==Diagnostics::Phase::Complete && job.snapshot.records.size()==20);
    mutate=true; job.start(false,0); finish(); REQUIRE(job.phase==Diagnostics::Phase::Failed && job.snapshot.records.size()==20); mutate=false;
    count=0; job.start(false,0); finish(); REQUIRE(job.phase==Diagnostics::Phase::Complete && job.snapshot.records.empty());
    stopped=false; job.start(true,0); finish(); REQUIRE(job.phase==Diagnostics::Phase::Failed && clears==0);
    stopped=true; clearReply=1; job.start(true,0); finish(); REQUIRE(job.clearResult==Diagnostics::ClearResult::Rejected && clears==1);
    clearReply=0; job.start(true,0); finish(); REQUIRE(job.phase==Diagnostics::Phase::Complete && clears==2);
    REQUIRE(job.clearResult==Diagnostics::ClearResult::Accepted && job.snapshot.clearPending && !job.snapshot.clearDurable);
    clearReply=-1; job.start(true,0); finish(); REQUIRE(job.phase==Diagnostics::Phase::Complete && clears==3);
    REQUIRE(job.clearResult==Diagnostics::ClearResult::Unknown); // Lost ACK is never retried.
    job.start(false,0); job.step(ecu,20000); REQUIRE(job.phase==Diagnostics::Phase::Failed);
    puts("PASS DTC units, labels, scan consistency, stale results, controller records and clear recovery");
}
