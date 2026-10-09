#include "autotune.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <type_traits>
const char* FeedbackModeName(FeedbackMode mode) {
    switch(mode) {
    case FeedbackMode::Auto: return "Auto";
    case FeedbackMode::WidebandOpenLoop: return "Wideband, open loop";
    case FeedbackMode::WidebandTrimCompensated: return "Wideband, trim compensated";
    case FeedbackMode::NarrowbandTrim: return "Narrowband trim learning";
    }
    return "Unknown";
}
const char* AutoTuneRejectName(AutoTuneReject reason) {
    static const char* names[]={
        "Calibration differs from ECU","Engine not running steadily (sync, cranking, DFCO, launch)",
        "Rev limit, cut or antilag","Output inhibit","Oxygen sensor type does not suit the mode",
        "RPM outside range","Load outside range","TPS outside range","Coolant below minimum",
        "Intake air above maximum","Enrichment active","Idle excluded",
        "Transient (TPS, RPM or MAP rate)","Settling after transient","Waiting for lambda delay",
        "Measured AFR implausible","Error above per-sample limit",
        "Closed-loop trim active","Closed-loop trim inactive","Target AFR outside narrowband window",
        "Trim at its limit","Narrowband status unavailable","Partial narrowband dwell discarded",
        "Outlier","Sample weight below cell minimum"};
    static_assert(sizeof(names)/sizeof(names[0])==size_t(AutoTuneReject::Count),"reason names");
    return names[size_t(reason)];
}
AutoTuneContext AutoTuneContext::FromCalibration(const unsigned char* c,bool matches) {
    AutoTuneContext x; x.calibrationMatches=matches;
    if(!c) return x;
    x.oxygenInput=c[0x600]; x.alphaN=(c[0x5D4]&1)!=0;
    x.windowMinAfr=c[0x8C8]*0.1f; x.windowMaxAfr=c[0x8C9]*0.1f;
    // STFT limits are Q7 factors (128 = neutral), as in lambda.c.
    x.trimLeanLimit=c[0x731]*100.0f/128-100; x.trimRichLimit=c[0x732]*100.0f/128-100;
    return x;
}
namespace {
template<class S,class F> void VisitSettings(S& s,F&& f) {
    f("mode",s.mode); f("delayBase",s.delayBase); f("delayCycles",s.delayCycles);
    f("rpmMin",s.rpmMin); f("rpmMax",s.rpmMax); f("loadMin",s.loadMin); f("loadMax",s.loadMax);
    f("tpsMin",s.tpsMin); f("tpsMax",s.tpsMax); f("cltMin",s.cltMin); f("iatMax",s.iatMax);
    f("tpsRateMax",s.tpsRateMax); f("rpmRateMax",s.rpmRateMax); f("mapRateMax",s.mapRateMax);
    f("settleTime",s.settleTime); f("excludeIdle",s.excludeIdle); f("allowWarmup",s.allowWarmup);
    f("allowAfterstart",s.allowAfterstart); f("allowAccel",s.allowAccel);
    f("afrMin",s.afrMin); f("afrMax",s.afrMax); f("maxErrorPercent",s.maxErrorPercent);
    f("minCellWeight",s.minCellWeight); f("minSwitches",s.minSwitches); f("trimLimitMargin",s.trimLimitMargin);
    f("minHits",s.minHits); f("minWeight",s.minWeight); f("maxStdDevPercent",s.maxStdDevPercent);
    f("outlierRejection",s.outlierRejection); f("resistance",s.resistance);
    f("authorityPercent",s.authorityPercent); f("maxStepPercent",s.maxStepPercent);
    f("veMin",s.veMin); f("veMax",s.veMax); f("deadbandPercent",s.deadbandPercent);
    f("smoothing",s.smoothing); f("advanced",s.advanced); f("autoApplyInterval",s.autoApplyInterval);
}
}
void AutoTuneSettings::applyPreset(Preset preset) {
    const AutoTuneSettings defaults;
    resistance=defaults.resistance; authorityPercent=defaults.authorityPercent;
    maxStepPercent=defaults.maxStepPercent; minCellWeight=defaults.minCellWeight;
    if(preset==Preset::Conservative) { resistance=0.25f; authorityPercent=10; minCellWeight=0.5f; }
    if(preset==Preset::Aggressive) { resistance=1; authorityPercent=30; }
}
bool AutoTuneSettings::sameErrorModel(const AutoTuneSettings& o) const {
    return mode==o.mode && delayBase==o.delayBase && delayCycles==o.delayCycles &&
        rpmMin==o.rpmMin && rpmMax==o.rpmMax && loadMin==o.loadMin && loadMax==o.loadMax &&
        tpsMin==o.tpsMin && tpsMax==o.tpsMax && cltMin==o.cltMin && iatMax==o.iatMax &&
        tpsRateMax==o.tpsRateMax && rpmRateMax==o.rpmRateMax && mapRateMax==o.mapRateMax &&
        settleTime==o.settleTime && excludeIdle==o.excludeIdle && allowWarmup==o.allowWarmup &&
        allowAfterstart==o.allowAfterstart && allowAccel==o.allowAccel && afrMin==o.afrMin &&
        afrMax==o.afrMax && maxErrorPercent==o.maxErrorPercent && minCellWeight==o.minCellWeight &&
        minSwitches==o.minSwitches && trimLimitMargin==o.trimLimitMargin && outlierRejection==o.outlierRejection;
}
std::string AutoTuneSettings::serialize() const {
    std::string out;
    VisitSettings(*this,[&](const char* key,const auto& v) {
        using T=std::decay_t<decltype(v)>;
        char line[96];
        if constexpr(std::is_same_v<T,float>) snprintf(line,sizeof(line),"%s=%.4g\n",key,v);
        else snprintf(line,sizeof(line),"%s=%d\n",key,int(v));
        out+=line;
    });
    return out;
}
void AutoTuneSettings::parse(const std::string& text) {
    std::istringstream in(text); std::string line;
    while(std::getline(in,line)) {
        const auto eq=line.find('='); if(eq==std::string::npos) continue;
        const std::string key=line.substr(0,eq); const char* value=line.c_str()+eq+1;
        char* end=nullptr; const double number=strtod(value,&end);
        if(end==value || !std::isfinite(number)) continue;
        VisitSettings(*this,[&](const char* name,auto& v) {
            if(key!=name) return;
            using T=std::decay_t<decltype(v)>;
            if constexpr(std::is_same_v<T,float>) v=float(number);
            else if constexpr(std::is_same_v<T,bool>) v=number!=0;
            else if constexpr(std::is_same_v<T,FeedbackMode>) v=FeedbackMode(std::clamp(int(number),0,3));
            else v=int(number);
        });
    }
    // Stored values are user-editable; keep them inside the UI ranges.
    resistance=std::clamp(resistance,0.05f,1.0f); authorityPercent=std::clamp(authorityPercent,1.0f,100.0f);
    maxStepPercent=std::clamp(maxStepPercent,0.0f,25.0f); deadbandPercent=std::clamp(deadbandPercent,0.0f,5.0f);
    veMin=std::clamp(veMin,0,255); veMax=std::clamp(veMax,veMin,255);
    delayBase=std::clamp(delayBase,0.0f,2.0f); delayCycles=std::clamp(delayCycles,0.0f,20.0f);
    minSwitches=std::clamp(minSwitches,2,40); autoApplyInterval=std::clamp(autoApplyInterval,2.0f,120.0f);
    autoApply=false;
}
double AutoTuneCellStats::stdDev() const {
    if(sumW<=0) return 0;
    const double m=mean(); return std::sqrt(std::max(0.0,sumWK2/sumW-m*m));
}
void AutoTune::clear() {
    cells={}; rejects={}; accepted=0; lastReject=AutoTuneReject::Count; resetHistory();
}
void AutoTune::resetHistory() {
    history.clear(); dwell={}; lastFrameTime=-1; lastTransient=-1e9f; okSince=-1;
}
void AutoTune::resetCells(const unsigned char* oldVe,const unsigned char* newVe) {
    std::array<bool,256> reset{};
    for(int i=0;i<256;++i) if(oldVe[i]!=newVe[i]) {
        const int row=i/16,col=i%16;
        for(int r=std::max(0,row-1);r<=std::min(15,row+1);++r)
            for(int c=std::max(0,col-1);c<=std::min(15,col+1);++c) reset[r*16+c]=true;
    }
    for(int i=0;i<256;++i) if(reset[i]) cells[i]={};
    // A running dwell may span a changed cell; its samples saw the old VE.
    if(dwell.base>=0 && (reset[dwell.base] || reset[dwell.base+1] || reset[dwell.base+16] || reset[dwell.base+17])) dwell={};
}
FeedbackMode AutoTune::resolveMode(const MonitorData& m,const AutoTuneContext& ctx) const {
    if(settings.mode!=FeedbackMode::Auto) return settings.mode;
    if(ctx.oxygenInput==0) return FeedbackMode::NarrowbandTrim;
    return (m.state&16) ? FeedbackMode::WidebandTrimCompensated : FeedbackMode::WidebandOpenLoop;
}
float AutoTune::delayFor(float rpm) const {
    return std::max(0.0f,settings.delayBase)+(rpm>0 ? std::max(0.0f,settings.delayCycles)*120.0f/rpm : 0.0f);
}
bool AutoTune::cellReady(int i) const {
    const auto& c=cells[i];
    return c.hits>=unsigned(std::max(1,settings.minHits)) && c.sumW>=settings.minWeight &&
        c.stdDev()*100<=settings.maxStdDevPercent;
}
bool AutoTune::reject(AutoTuneReject reason,const char* text) {
    ++rejects[size_t(reason)]; lastReject=reason; status=text; return false;
}
void AutoTune::add(int cell,double k,double w,unsigned hits,float time) {
    auto& c=cells[cell]; c.sumW+=w; c.sumWK+=w*k; c.sumWK2+=w*k*k; c.hits+=hits; c.lastHit=time;
}
bool AutoTune::outlier(int cell,double k) const {
    const auto& c=cells[cell];
    if(!settings.outlierRejection || c.hits<10) return false;
    // A floor stops a very consistent cell from rejecting every new sample.
    return std::abs(k-c.mean())>3*std::max(c.stdDev(),0.005);
}
void AutoTune::sample(const MonitorData& m,float time,const AutoTuneContext& ctx) {
    if(!collecting) { resetHistory(); status="Collection is off"; return; }
    const auto& s=settings;
    const float dt=lastFrameTime>=0 ? time-lastFrameTime : -1;
    const bool continuous=dt>0 && dt<0.25f && m.generation==lastFrame.generation &&
        m.lossOfSyncCount==lastFrame.lossOfSyncCount;
    const MonitorData prev=lastFrame; lastFrameTime=time; lastFrame=m;
    if(!continuous) { history.clear(); okSince=-1; dwell={}; }
    if(continuous && (std::abs(m.tps-prev.tps)/dt>s.tpsRateMax ||
        std::abs(float(m.rpm)-float(prev.rpm))/dt>s.rpmRateMax ||
        std::abs(float(m.kpa)-float(prev.kpa))/dt>s.mapRateMax)) lastTransient=time;
    activeMode=resolveMode(m,ctx);
    const bool narrowband=activeMode==FeedbackMode::NarrowbandTrim;
    // Hard gates: the frame cannot describe the fuel the engine received, so it
    // also breaks the delay window and any narrowband dwell.
    auto fail=[&](AutoTuneReject reason,const char* text) {
        history.push_back({time,m,false}); okSince=-1;
        if(dwell.sumDt>0) ++rejects[size_t(AutoTuneReject::PartialDwell)];
        dwell={};
        while(history.size()>1 && history.front().time<time-8) history.pop_front();
        reject(reason,text);
    };
    const float load=ctx.alphaN ? m.tps : float(m.kpa);
    const bool idle=m.detailAvailable ? (m.idleMode>=1 && m.idleMode<=4) : m.rpm<m.idleTarget+200;
    if(!m.valid) return fail(AutoTuneReject::EngineState,"Waiting for a live frame");
    if(!ctx.calibrationMatches) return fail(AutoTuneReject::Calibration,"Write or read the active tune so the calibration matches the ECU");
    if(narrowband ? ctx.oxygenInput!=0 : ctx.oxygenInput!=1)
        return fail(AutoTuneReject::SensorMode,narrowband?"Narrowband mode needs oxygen input set to narrowband":"Wideband modes need oxygen input set to wideband");
    if(!m.synced() || !(m.state&4) || (m.state&(2|8|128))) return fail(AutoTuneReject::EngineState,"Waiting for steady running");
    if(m.flags&0xAF) return fail(AutoTuneReject::CutOrLimit,"Rev limit, cut, antilag, storage or staging active");
    if(m.inhibits) return fail(AutoTuneReject::Inhibit,"Output inhibit active");
    if(m.rpm<s.rpmMin || m.rpm>s.rpmMax) return fail(AutoTuneReject::RpmRange,"RPM outside the sampling range");
    if(load<s.loadMin || load>s.loadMax) return fail(AutoTuneReject::LoadRange,"Load outside the sampling range");
    if(m.tps<s.tpsMin || m.tps>s.tpsMax) return fail(AutoTuneReject::TpsRange,"TPS outside the sampling range");
    if(m.clt<s.cltMin) return fail(AutoTuneReject::Coolant,"Waiting for coolant temperature");
    if(m.iat>s.iatMax) return fail(AutoTuneReject::IntakeAir,"Intake air too hot");
    if((!s.allowWarmup && m.warmup!=100) || (!s.allowAfterstart && m.afterstart!=100) || (!s.allowAccel && m.accel!=100))
        return fail(AutoTuneReject::Enrichment,"Waiting for enrichments to finish");
    if(s.excludeIdle && idle) return fail(AutoTuneReject::Idle,"Idle is excluded");
    if(lastTransient==time) return fail(AutoTuneReject::Transient,"Transient: hold throttle, RPM and MAP steady");
    if(time-lastTransient<s.settleTime) return fail(AutoTuneReject::Settling,"Settling after a transient");
    history.push_back({time,m,true});
    if(okSince<0) okSince=time;
    while(history.size()>1 && history.front().time<time-8) history.pop_front();
    // Attribute the measurement to the plan one transport delay earlier.
    const float planTime=time-delayFor(float(m.rpm));
    if(okSince>planTime) return void(reject(AutoTuneReject::Delay,"Waiting for steady samples and lambda delay"));
    const Frame* planFrame=nullptr;
    for(auto it=history.rbegin();it!=history.rend();++it) if(it->time<=planTime) { planFrame=&*it; break; }
    if(!planFrame || planTime-planFrame->time>=0.25f) return void(reject(AutoTuneReject::Delay,"Waiting for steady samples and lambda delay"));
    const MonitorData& plan=planFrame->data;
    const double x=plan.rpmFraction,y=plan.loadFraction;
    const double weights[4]={(1-x)*(1-y),x*(1-y),(1-x)*y,x*y};
    const int base=plan.cellLoad*16+plan.cellRpm;
    const int indices[4]={base,base+1,base+16,base+17};
    int dominant=0; for(int i=1;i<4;++i) if(weights[i]>weights[dominant]) dominant=i;
    auto used=[&](int i) { return weights[i]>0 && weights[i]>=s.minCellWeight; };
    if(!used(dominant)) return void(reject(AutoTuneReject::LowWeight,"Sample weight below the cell minimum"));
    if(!narrowband) {
        if(activeMode==FeedbackMode::WidebandOpenLoop && (plan.state&16) && std::abs(plan.trimPercent)>0.05f)
            return void(reject(AutoTuneReject::TrimActive,"Closed-loop trim is active; use Auto or trim-compensated mode"));
        if(m.measuredAfr<s.afrMin || m.measuredAfr>s.afrMax || plan.targetAfr<=0)
            return void(reject(AutoTuneReject::AfrImplausible,"Measured AFR outside the plausible range"));
        double k=m.measuredAfr/plan.targetAfr;
        if(activeMode==FeedbackMode::WidebandTrimCompensated) k*=1+plan.trimPercent/100.0;
        if(std::abs(k-1)*100>s.maxErrorPercent) return void(reject(AutoTuneReject::ErrorTooLarge,"AFR error exceeds the per-sample limit"));
        if(outlier(indices[dominant],k)) return void(reject(AutoTuneReject::Outlier,"Sample rejected as an outlier"));
        for(int i=0;i<4;++i) if(used(i)) add(indices[i],k,weights[i],1,time);
        ++accepted; lastReject=AutoTuneReject::Count; status="Collecting VE corrections"; return;
    }
    // Narrowband: the trim corrects the fuel delivered one delay earlier, so it
    // belongs to the plan cell, but its own value is current.
    auto softFail=[&](AutoTuneReject reason,const char* text) {
        if(dwell.sumDt>0) ++rejects[size_t(AutoTuneReject::PartialDwell)];
        dwell={}; reject(reason,text);
    };
    if(!(m.state&16)) return softFail(AutoTuneReject::TrimInactive,"Waiting for closed-loop trim to become active");
    if(m.targetAfr<ctx.windowMinAfr-0.05f || m.targetAfr>ctx.windowMaxAfr+0.05f)
        return softFail(AutoTuneReject::TargetOutsideWindow,"Target AFR outside the narrowband window");
    if(m.narrowbandBand==0) return softFail(AutoTuneReject::BandUnavailable,"Narrowband status unavailable");
    if(m.trimPercent>=ctx.trimLeanLimit-s.trimLimitMargin || m.trimPercent<=ctx.trimRichLimit+s.trimLimitMargin)
        return softFail(AutoTuneReject::TrimAtLimit,"Trim at its limit: the real error is unknown");
    if(dwell.base!=base) {
        if(dwell.sumDt>0) ++rejects[size_t(AutoTuneReject::PartialDwell)];
        dwell={}; dwell.base=base;
    }
    // Average only over complete switching cycles, transition to transition:
    // switches counts the transition that opened the segment as 1.
    const int side=m.narrowbandBand==1 ? -1 : m.narrowbandBand==4 ? 1 : 0;
    if(side && dwell.lastSide && side!=dwell.lastSide) ++dwell.switches;
    if(side) dwell.lastSide=side;
    const bool started=dwell.switches>0;
    if(dwell.switches>s.minSwitches && dwell.sumDt>0) {
        const double k=1+dwell.sumTrim/dwell.sumDt/100.0;
        const int dom=int(std::max_element(dwell.w.begin(),dwell.w.end())-dwell.w.begin());
        dwell.switches=1;
        const auto w=dwell.w; const auto hits=dwell.hits;
        dwell.sumDt=dwell.sumTrim=0; dwell.w={}; dwell.hits={};
        if(std::abs(k-1)*100>s.maxErrorPercent) reject(AutoTuneReject::ErrorTooLarge,"Trim average exceeds the per-sample limit");
        else if(outlier(indices[dom],k)) reject(AutoTuneReject::Outlier,"Dwell average rejected as an outlier");
        else for(int i=0;i<4;++i) if(hits[i]) add(indices[i],k,w[i],hits[i],time);
    }
    lastReject=AutoTuneReject::Count;
    if(!started) { status="Waiting for narrowband switching"; ++accepted; return; }
    const double step=std::clamp(double(dt),0.0,0.25);
    dwell.sumDt+=step; dwell.sumTrim+=step*m.trimPercent;
    for(int i=0;i<4;++i) if(used(i)) { dwell.w[i]+=weights[i]; ++dwell.hits[i]; }
    ++accepted; status="Learning from narrowband trim";
}
int AutoTune::propose(const CalBuffer& source,CalBuffer& result,std::array<AutoTuneCellProposal,256>* detail) const {
    result=source;
    std::array<AutoTuneCellProposal,256> local; auto& out=detail ? *detail : local;
    const auto& s=settings;
    const double resistance=std::clamp(double(s.resistance),0.05,1.0);
    const double maxStep=std::max(0.0,double(s.maxStepPercent))/100;
    std::array<double,256> correction{}; std::array<bool,256> has{};
    for(int i=0;i<256;++i) {
        auto& p=out[i]; p={};
        p.current=p.proposed=source.data[i]; p.exact=p.current;
        p.baseline=baselineValid ? baseline[i] : p.current;
        p.locked=locks[i]; p.ready=cellReady(i);
        if(!p.ready || p.locked || !p.current) continue;
        const double error=cells[i].mean()-1;
        if(std::abs(error)*100<s.deadbandPercent) { p.deadband=true; continue; }
        double c=error*resistance;
        if(maxStep>0) c=std::clamp(c,-maxStep,maxStep);
        correction[i]=c; has[i]=true;
    }
    if(s.smoothing) {
        // Blend with ready unlocked neighbours, weighted by accumulated weight.
        auto blended=correction;
        for(int i=0;i<256;++i) if(has[i]) {
            double sum=0,total=0; const int row=i/16,col=i%16;
            for(int r=std::max(0,row-1);r<=std::min(15,row+1);++r)
                for(int c=std::max(0,col-1);c<=std::min(15,col+1);++c) {
                    const int j=r*16+c; if(!has[j]) continue;
                    sum+=correction[j]*cells[j].sumW; total+=cells[j].sumW;
                }
            if(total>0) blended[i]=sum/total;
        }
        correction=blended;
    }
    int changed=0;
    for(int i=0;i<256;++i) if(has[i]) {
        auto& p=out[i]; const double c=correction[i]; const int current=p.current;
        p.correction=c; p.exact=current*(1+c);
        const double auth=std::clamp(double(s.authorityPercent),1.0,100.0)/100;
        const double lo=p.baseline*(1-auth),hi=p.baseline*(1+auth);
        int v=int(std::lround(p.exact));
        // Rounding must not take a single update past the step limit.
        if(maxStep>0 && std::abs(v-current)>current*maxStep+1e-9) v=current;
        if(c>0) {
            p.atAuthority=p.exact>hi+1e-9;
            v=std::min(v,std::max(current,int(std::floor(hi+1e-9))));
            v=std::min(v,std::max(current,s.veMax));
        } else {
            p.atAuthority=p.exact<lo-1e-9;
            v=std::max(v,std::min(current,int(std::ceil(lo-1e-9))));
            v=std::max(v,std::min(current,s.veMin));
        }
        v=std::clamp(v,0,255);
        p.proposed=v;
        p.blockedByResolution=v==current && !p.atAuthority;
        if(v!=current) { result.data[i]=(unsigned char)v; ++changed; }
    }
    result.recomputeDirty(); return changed;
}
