#include "autotune.h"
#include <algorithm>
#include <cmath>
void AutoTune::sample(const MonitorData& m,float time,bool wideband) {
    if(!collecting) { history.clear(); return; }
    const bool eligible=wideband && m.valid && m.synced() && (m.state&4) &&
        !(m.state&(2|8|16|128)) && !(m.flags&0xAF) && !m.inhibits && m.measuredAfr>0 &&
        m.targetAfr>0 && m.afterstart==100 && m.accel==100 && m.warmup==100 &&
        m.cellRpm<15 && m.cellLoad<15;
    bool steady=false;
    if(!history.empty()) {
        const auto& last=history.back(); float dt=time-last.time;
        steady=dt>0 && dt<0.25f && std::abs(m.tps-last.data.tps)/dt<2 &&
            std::abs(float(m.rpm)-last.data.rpm)/dt<400 &&
            m.generation==last.data.generation && m.lossOfSyncCount==last.data.lossOfSyncCount;
    }
    if(!eligible) { history.clear(); status="Waiting for warm steady running, wideband and closed-loop off"; return; }
    history.push_back({time,m,steady});
    const float delay=std::clamp(delaySeconds,0.0f,2.0f);
    while(history.size()>2 && history[1].time<=time-delay) history.pop_front();
    if(history.empty() || time-history.front().time<delay || time-history.front().time>delay+0.25f || !steady) {
        status="Waiting for steady samples and exhaust delay"; return;
    }
    for(const auto& s:history) if(!s.steady) { status="Waiting for steady samples"; return; }
    const auto& plan=history.front().data;
    const double ratio=m.measuredAfr/plan.targetAfr;
    if(ratio<0.8 || ratio>1.2) { status="AFR error exceeds the suggestion limit"; return; }
    const double x=plan.rpmFraction,y=plan.loadFraction;
    const double weights[]={(1-x)*(1-y),x*(1-y),(1-x)*y,x*y};
    const int at=plan.cellLoad*16+plan.cellRpm;
    const int indices[]={at,at+1,at+16,at+17};
    for(int i=0;i<4;++i) if(weights[i]>0.01) {
        auto& c=cells[indices[i]]; c.error+=(ratio-1)*weights[i]; c.weight+=weights[i]; ++c.samples;
    }
    status="Collecting bounded VE suggestions";
}
int AutoTune::propose(const CalBuffer& source,CalBuffer& result) const {
    result=source; int changed=0;
    for(int i=0;i<256;++i) {
        const auto& c=cells[i];
        if(c.samples<20 || c.weight<10 || !source.data[i]) continue;
        double correction=std::clamp(c.error/c.weight,-0.02,0.02);
        int value=int(std::lround(source.data[i]*(1+correction)));
        // No forced one-unit step when it would exceed the 2% bound.
        if(std::abs(value-int(source.data[i]))>source.data[i]*0.02) value=source.data[i];
        if(value!=source.data[i]) { result.data[i]=(unsigned char)std::clamp(value,0,255); ++changed; }
    }
    result.recomputeDirty(); return changed;
}
