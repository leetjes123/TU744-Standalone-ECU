#include "autotune.h"
#include "autotune_log.h"
#include "monitor_channels.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL autotune line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
namespace {
using R=AutoTuneReject;
MonitorData Steady() {
    MonitorData m; m.valid=true; m.state=5; m.warmup=m.afterstart=m.accel=100;
    m.rpm=2500; m.kpa=60; m.tps=10; m.clt=90; m.iat=30; m.idleTarget=900;
    m.targetAfr=14; m.measuredAfr=15; m.cellRpm=2; m.cellLoad=3; m.narrowbandBand=2;
    return m;
}
AutoTuneContext Wideband() {
    AutoTuneContext c; c.calibrationMatches=true; c.oxygenInput=1;
    c.windowMinAfr=14.2f; c.windowMaxAfr=15.2f; c.trimLeanLimit=9.375f; c.trimRichLimit=-21.875f;
    return c;
}
AutoTuneContext Narrowband() { auto c=Wideband(); c.oxygenInput=0; return c; }
unsigned Count(const AutoTune& t,R r) { return t.rejects[size_t(r)]; }
void Run(AutoTune& t,const MonitorData& m,const AutoTuneContext& c,int n,float start=0) {
    for(int i=0;i<n;++i) t.sample(m,start+i*.05f,c);
}
// Frames with a +4 % sawtooth trim: lean (rising) and rich (falling) halves of 10 frames.
void Sawtooth(AutoTune& t,const AutoTuneContext& c,int n,MonitorData m=Steady()) {
    m.state|=16; m.targetAfr=14.7f; m.measuredAfr=0;
    for(int i=0;i<n;++i) {
        const int j=i%20; const bool lean=j<10;
        m.narrowbandBand=lean?1:4; m.trimPercent=lean?2+0.4f*j:6-0.4f*(j-10);
        t.sample(m,i*.05f,c);
    }
}
CalBuffer Tune() { CalBuffer c; CHECK(c.loadFromFile(TEST_TUNE_PATH)); CHECK(c.data[50]==60); return c; }
}
void AutotuneTests() {
    const CalBuffer original=Tune(); CalBuffer proposed;
    std::array<AutoTuneCellProposal,256> detail;
    // 1. Wideband open loop: the old 2 % step reproduces 50 -> 61; Easy gives the full +7 %.
    {
        AutoTune t; t.collecting=true; t.settings.maxStepPercent=2;
        Run(t,Steady(),Wideband(),100);
        CHECK(t.activeMode==FeedbackMode::WidebandOpenLoop);
        CHECK(t.propose(original,proposed)==1 && proposed.data[50]==61);
        t.settings.resistance=1; t.settings.maxStepPercent=0;
        CHECK(t.propose(original,proposed)==1 && proposed.data[50]==64);
        // Explicit open-loop mode rejects samples taken with trim applied.
        AutoTune open; open.collecting=true; open.settings.mode=FeedbackMode::WidebandOpenLoop;
        auto m=Steady(); m.state|=16; m.trimPercent=3; Run(open,m,Wideband(),100);
        CHECK(open.propose(original,proposed)==0 && Count(open,R::TrimActive)>0);
        // Wideband modes need a wideband input.
        AutoTune wrong; wrong.collecting=true; Run(wrong,Steady(),Narrowband(),20);
        CHECK(wrong.activeMode==FeedbackMode::NarrowbandTrim && Count(wrong,R::SensorMode)==0);
        wrong.settings.mode=FeedbackMode::WidebandOpenLoop; wrong.clear(); Run(wrong,Steady(),Narrowband(),20);
        CHECK(Count(wrong,R::SensorMode)==20 && wrong.accepted==0);
    }
    // 2. Wideband with trim active: on-target AFR with +5 % trim means VE is 5 % low.
    {
        AutoTune t; t.collecting=true; t.settings.resistance=1;
        auto m=Steady(); m.state|=16; m.measuredAfr=14; m.trimPercent=5; Run(t,m,Wideband(),100);
        CHECK(t.activeMode==FeedbackMode::WidebandTrimCompensated);
        CHECK(t.propose(original,proposed)==1 && proposed.data[50]==63);
        t.settings.authorityPercent=3; t.propose(original,proposed,&detail);
        CHECK(proposed.data[50]==61 && detail[50].atAuthority);
    }
    // 3. Narrowband trim learning averages complete switching cycles.
    {
        AutoTune t; t.collecting=true; t.settings.resistance=1;
        Sawtooth(t,Narrowband(),400);
        CHECK(t.activeMode==FeedbackMode::NarrowbandTrim && t.cellReady(50));
        CHECK(std::abs(t.cells[50].mean()-1.04)<0.003);
        CHECK(t.propose(original,proposed,&detail)==1 && proposed.data[50]==62);
        CHECK(std::abs(detail[50].correction-0.04)<0.003);
        AutoTune few; few.collecting=true; Sawtooth(few,Narrowband(),35);
        CHECK(few.cells[50].hits==0 && few.propose(original,proposed)==0);
    }
    // 4. Narrowband gates each count their own reason.
    {
        auto m=Steady(); m.state|=16; m.targetAfr=13; m.narrowbandBand=1; m.trimPercent=2;
        AutoTune a; a.collecting=true; Run(a,m,Narrowband(),30);
        CHECK(Count(a,R::TargetOutsideWindow)>0 && a.cells[50].hits==0);
        m.targetAfr=14.7f; m.trimPercent=9;
        AutoTune b; b.collecting=true; Run(b,m,Narrowband(),30); CHECK(Count(b,R::TrimAtLimit)>0);
        m.trimPercent=-21; AutoTune b2; b2.collecting=true; Run(b2,m,Narrowband(),30); CHECK(Count(b2,R::TrimAtLimit)>0);
        m.trimPercent=2; m.narrowbandBand=0;
        AutoTune c; c.collecting=true; Run(c,m,Narrowband(),30); CHECK(Count(c,R::BandUnavailable)>0);
        m.narrowbandBand=1; m.state&=~16;
        AutoTune d; d.collecting=true; Run(d,m,Narrowband(),30); CHECK(Count(d,R::TrimInactive)>0);
        CHECK(Count(a,R::TrimAtLimit)==0 && Count(b,R::TargetOutsideWindow)==0);
    }
    // 5. Delay model: measurements belong to the plan one delay earlier.
    {
        AutoTune t; CHECK(std::abs(t.delayFor(6000)-0.17f)<0.001f && std::abs(t.delayFor(800)-0.95f)<0.001f);
        t.settings.delayCycles=0; t.settings.delayBase=0.4f; CHECK(t.delayFor(800)==0.4f);
        t=AutoTune{}; t.collecting=true; auto m=Steady();
        Run(t,m,Wideband(),40);                        // up to t=1.95 in cell 50
        const unsigned before=t.cells[50].hits;
        m.cellRpm=5; Run(t,m,Wideband(),4,2.0f);         // 0.15 s later, within d=0.338 s
        CHECK(t.cells[53].hits==0 && t.cells[50].hits==before+4);
        Run(t,m,Wideband(),20,2.2f);
        CHECK(t.cells[53].hits>0 && t.cells[50].hits<before+4+8);
    }
    // 6. Configurable filters reject at their boundaries; settle time follows a TPS step.
    {
        auto check=[&](auto edit,R reason,bool rejected) {
            AutoTune t; t.collecting=true; auto m=Steady(); edit(t.settings,m);
            Run(t,m,Wideband(),3); CHECK((Count(t,reason)>0)==rejected);
        };
        using S=AutoTuneSettings;
        check([](S&,MonitorData& m) { m.rpm=599; },R::RpmRange,true);
        check([](S&,MonitorData& m) { m.rpm=600; },R::RpmRange,false);
        check([](S& s,MonitorData& m) { s.rpmMax=3000; m.rpm=3001; },R::RpmRange,true);
        check([](S& s,MonitorData& m) { s.loadMax=100; m.kpa=101; },R::LoadRange,true);
        check([](S& s,MonitorData& m) { s.loadMax=100; m.kpa=100; },R::LoadRange,false);
        check([](S& s,MonitorData& m) { s.tpsMin=20; m.tps=19.9f; },R::TpsRange,true);
        check([](S& s,MonitorData& m) { s.tpsMin=20; m.tps=20; },R::TpsRange,false);
        check([](S&,MonitorData& m) { m.clt=69; },R::Coolant,true);
        check([](S&,MonitorData& m) { m.clt=70; },R::Coolant,false);
        check([](S&,MonitorData& m) { m.iat=61; },R::IntakeAir,true);
        check([](S&,MonitorData& m) { m.iat=60; },R::IntakeAir,false);
        check([](S&,MonitorData& m) { m.warmup=101; },R::Enrichment,true);
        check([](S& s,MonitorData& m) { s.allowWarmup=true; m.warmup=101; },R::Enrichment,false);
        check([](S& s,MonitorData& m) { s.excludeIdle=true; m.rpm=1099; },R::Idle,true);
        check([](S& s,MonitorData& m) { s.excludeIdle=true; m.rpm=1100; },R::Idle,false);
        check([](S& s,MonitorData& m) { s.excludeIdle=true; m.detailAvailable=true; m.idleMode=3; },R::Idle,true);
        check([](S&,MonitorData& m) { m.flags=1; },R::CutOrLimit,true);
        check([](S&,MonitorData& m) { m.state|=8; },R::EngineState,true);
        check([](S&,MonitorData& m) { m.inhibits=4; },R::Inhibit,true);
        AutoTune t; t.collecting=true; auto m=Steady(); Run(t,m,Wideband(),20);
        m.tps=11; t.sample(m,1.0f,Wideband());          // 20 %/s
        CHECK(Count(t,R::Transient)==1);
        Run(t,m,Wideband(),9,1.05f); CHECK(Count(t,R::Settling)==9);
        AutoTune u; u.collecting=true; m=Steady(); Run(u,m,Wideband(),20);
        m.kpa=62; u.sample(m,1.0f,Wideband()); CHECK(Count(u,R::Transient)==1);   // 40 kPa/s
        Run(u,m,Wideband(),9,1.05f); CHECK(Count(u,R::Settling)==9);
        Run(u,m,Wideband(),2,1.5f); CHECK(Count(u,R::Settling)==9);
    }
    // 7. Outliers, deadband, resistance, locks, VE bounds and resolution.
    {
        AutoTune t; t.collecting=true; auto m=Steady(); m.measuredAfr=14.84f; Run(t,m,Wideband(),40);   // +6 %
        const unsigned hits=t.cells[50].hits; CHECK(hits>=20);
        m.measuredAfr=16; t.sample(m,2.0f,Wideband());
        CHECK(Count(t,R::Outlier)==1 && t.cells[50].hits==hits);
        t.settings.resistance=0.5f; CHECK(t.propose(original,proposed)==1 && proposed.data[50]==62);
        t.settings.resistance=0.25f; CHECK(t.propose(original,proposed)==1 && proposed.data[50]==61);
        t.settings.resistance=1; t.settings.veMax=61; CHECK(t.propose(original,proposed)==1 && proposed.data[50]==61);
        t.settings.veMax=255; t.locks[50]=true;
        CHECK(t.propose(original,proposed,&detail)==0 && detail[50].locked); t.locks[50]=false;
        t.settings.deadbandPercent=6.1f; CHECK(t.propose(original,proposed,&detail)==0 && detail[50].deadband);
        t.settings.deadbandPercent=0.5f;
        CalBuffer lowVe=original; lowVe.data[50]=8; t.settings.resistance=0.5f;
        CHECK(t.propose(lowVe,proposed,&detail)==0 && detail[50].blockedByResolution && !detail[50].atAuthority);
        // Lower VE: the error is lean, so VE min only limits a rich correction.
        AutoTune rich; rich.collecting=true; m=Steady(); m.measuredAfr=13.3f; Run(rich,m,Wideband(),40);
        rich.settings.resistance=1; rich.settings.veMin=59;
        CHECK(rich.propose(original,proposed)==1 && proposed.data[50]==59);
        AutoTune d; d.collecting=true; m=Steady(); m.measuredAfr=14.05f; Run(d,m,Wideband(),40);
        CHECK(d.propose(original,proposed,&detail)==0 && detail[50].deadband);
        // Readiness also needs consistent samples.
        AutoTune noisy; noisy.collecting=true; noisy.settings.outlierRejection=false; m=Steady();
        for(int i=0;i<60;++i) { m.measuredAfr=i%2?13:15.4f; noisy.sample(m,i*.05f,Wideband()); }
        CHECK(noisy.cells[50].stdDev()>0.03 && !noisy.cellReady(50));
    }
    // 8. Per-cell reset clears changed cells and their neighbours only.
    {
        AutoTune t; for(auto& c:t.cells) c.hits=5;
        unsigned char oldVe[256],newVe[256]; memcpy(oldVe,original.data,256); memcpy(newVe,oldVe,256);
        newVe[50]++; newVe[255]++;
        t.resetCells(oldVe,newVe);
        int cleared=0; for(auto& c:t.cells) cleared+=c.hits==0;
        CHECK(cleared==13);
        for(int i:{33,34,35,49,50,51,65,66,67,238,239,254,255}) CHECK(t.cells[i].hits==0);
        CHECK(t.cells[32].hits==5 && t.cells[52].hits==5 && t.cells[18].hits==5);
    }
    // 9. Repeated application never moves a cell past its authority limit.
    {
        AutoTune t; t.collecting=true; t.settings.resistance=1; t.settings.authorityPercent=10;
        t.setBaseline(original.data);
        auto m=Steady(); m.measuredAfr=16.8f; Run(t,m,Wideband(),60);   // +20 %
        CalBuffer cal=original;
        for(int pass=0;pass<10;++pass) {
            t.propose(cal,proposed,&detail);
            memcpy(cal.data,proposed.data,256);
            CHECK(cal.data[50]<=66);
        }
        CHECK(cal.data[50]==66 && detail[50].atAuthority && t.propose(cal,proposed)==0);
    }
    // 10. Log replay of a CSV gives the same statistics and proposals as live sampling.
    {
        std::vector<MonitorData> frames; auto m=Steady(); m.rpmFraction=0.25f; m.loadFraction=0.5f;
        for(int i=0;i<200;++i) { m.measuredAfr=i<100?15:14.5f; m.cellRpm=i<100?2:3; frames.push_back(m); }
        AutoTune live; live.collecting=true;
        for(size_t i=0;i<frames.size();++i) live.sample(frames[i],i*.05f,Wideband());
        const char* path="autotune-replay.csv";
        FILE* f=fopen(path,"w"); CHECK(f);
        fprintf(f,"Time(s)"); for(int s=0;s<SIG_COUNT;++s) fprintf(f,",%s",MonitorColumnName(s).c_str()); fprintf(f,"\n");
        for(size_t i=0;i<frames.size();++i) {
            fprintf(f,"%.3f",i*.05f); for(int s=0;s<SIG_COUNT;++s) fprintf(f,",%.3f",MonitorSignalValue(s,frames[i])); fprintf(f,"\n");
        }
        fclose(f);
        std::vector<AutoTuneLogFrame> logged; AutoTuneLogReport report;
        CHECK(LoadAutotuneLog(path,logged,report) && logged.size()==frames.size() && report.missingColumns.empty());
        CHECK(report.generations.size()==1);
        AutoTune replay; ReplayAutotuneLog(replay,logged,Wideband(),original.data,report);
        CHECK(!replay.collecting && replay.accepted==live.accepted);
        for(int i=0;i<256;++i) CHECK(replay.cells[i].hits==live.cells[i].hits && std::abs(replay.cells[i].sumW-live.cells[i].sumW)<1e-6);
        CalBuffer a,b; CHECK(live.propose(original,a)>0 && replay.propose(original,b)==live.propose(original,a));
        CHECK(!memcmp(a.data,b.data,256));
        CHECK(report.runningRows==200 && report.veMismatchRows==200);   // VE channel was 0 in these frames
        remove(path);
        AutoTuneLogReport missing; f=fopen(path,"w"); CHECK(f); fprintf(f,"Time(s),Engine Speed(rpm)\n0,1000\n"); fclose(f);
        CHECK(!LoadAutotuneLog(path,logged,missing) && !missing.missingColumns.empty()); remove(path);
    }
    // Settings round-trip and error-model classification.
    {
        AutoTuneSettings s; s.mode=FeedbackMode::NarrowbandTrim; s.delayCycles=4; s.excludeIdle=true; s.authorityPercent=12.5f;
        AutoTuneSettings r; r.parse(s.serialize());
        CHECK(r.mode==s.mode && r.delayCycles==4 && r.excludeIdle && r.authorityPercent==12.5f && r.sameErrorModel(s));
        r.authorityPercent=30; CHECK(r.sameErrorModel(s)); r.cltMin=60; CHECK(!r.sameErrorModel(s));
        r.applyPreset(AutoTuneSettings::Preset::Conservative); CHECK(r.resistance==0.25f && r.authorityPercent==10 && r.minCellWeight==0.5f);
    }
    printf("PASS autotune modes, delay, filters, narrowband dwell, limits, per-cell reset and log replay\n");
}
