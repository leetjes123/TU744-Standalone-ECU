#include "autotune_log.h"
#include "monitor_channels.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
void Split(const std::string& line,std::vector<std::string>& out) {
    out.clear(); size_t start=0;
    while(true) {
        const size_t comma=line.find(',',start);
        out.push_back(line.substr(start,comma==std::string::npos?std::string::npos:comma-start));
        if(comma==std::string::npos) break;
        start=comma+1;
    }
    for(auto& f:out) {
        while(!f.empty() && (f.back()=='\r' || f.back()=='\n' || f.back()==' ')) f.pop_back();
        if(f.size()>=2 && f.front()=='"' && f.back()=='"') f=f.substr(1,f.size()-2);
    }
}
// Inverse of MonitorSignalValue for the channels the autotune gates read.
bool Assign(int signal,float v,MonitorData& m) {
    if(!std::isfinite(v)) return false;
    switch(signal) {
    case SIG_RPM: m.rpm=uint16_t(std::lround(v)); break;
    case SIG_MAP: m.kpa=uint16_t(std::lround(v)); break;
    case SIG_TPS: m.tps=v; break;
    case SIG_AFR: m.measuredAfr=v; break;
    case SIG_TARGET_AFR: m.targetAfr=v; break;
    case SIG_VE: m.ve=uint16_t(std::lround(v)); break;
    case SIG_CLT: m.clt=int(std::lround(v)); break;
    case SIG_IAT: m.iat=int(std::lround(v)); break;
    case SIG_STFT: m.trimPercent=v; break;
    case SIG_OXYGEN: m.oxygenMv=int(std::lround(v)); break;
    case SIG_TARGET_IDLE: m.idleTarget=int(std::lround(v)); break;
    case SIG_WARMUP_ENRICH: m.warmup=int(std::lround(v)); break;
    case SIG_ASE: m.afterstart=int(std::lround(v)); break;
    case SIG_ACCEL_ENRICH: m.accel=int(std::lround(v)); break;
    case SIG_SYNC_LOSS: m.lossOfSyncCount=uint16_t(std::lround(v)); break;
    case SIG_CELL_RPM: m.cellRpm=int(std::lround(v)); break;
    case SIG_RPM_FRACTION: m.rpmFraction=v; break;
    case SIG_CELL_LOAD: m.cellLoad=int(std::lround(v)); break;
    case SIG_LOAD_FRACTION: m.loadFraction=v; break;
    case SIG_STATE: m.state=uint8_t(std::lround(v)); break;
    case SIG_FLAGS: m.flags=uint8_t(std::lround(v)); break;
    case SIG_INHIBITS: m.inhibits=uint16_t(std::lround(v)); break;
    case SIG_GENERATION: m.generation=uint16_t(std::lround(v)); break;
    case SIG_NB_BAND: m.narrowbandBand=int(std::lround(v)); break;
    case SIG_IDLE_MODE: m.idleMode=uint8_t(std::lround(v)); m.detailAvailable=true; break;
    default: return false;
    }
    return true;
}
const int REQUIRED[]={SIG_RPM,SIG_MAP,SIG_TPS,SIG_AFR,SIG_TARGET_AFR,SIG_VE,SIG_CLT,SIG_IAT,SIG_STFT,
    SIG_WARMUP_ENRICH,SIG_ASE,SIG_ACCEL_ENRICH,SIG_CELL_RPM,SIG_RPM_FRACTION,SIG_CELL_LOAD,
    SIG_LOAD_FRACTION,SIG_STATE,SIG_FLAGS,SIG_INHIBITS,SIG_GENERATION,SIG_NB_BAND};
}
bool LoadAutotuneLog(const char* path,std::vector<AutoTuneLogFrame>& frames,AutoTuneLogReport& report) {
    frames.clear(); report=AutoTuneLogReport{}; report.path=path?path:"";
    FILE* f=path?fopen(path,"rb"):nullptr;
    if(!f) { report.error="Could not open the log file"; return false; }
    std::string line; std::vector<std::string> fields; std::vector<int> map; int timeColumn=-1;
    char buffer[8192]; bool header=true;
    auto readLine=[&]() {
        line.clear();
        while(fgets(buffer,sizeof(buffer),f)) { line+=buffer; if(!line.empty() && line.back()=='\n') return true; }
        return !line.empty();
    };
    while(readLine()) {
        Split(line,fields);
        if(header) {
            if(!fields.empty() && fields[0].size()>=3 && (unsigned char)fields[0][0]==0xEF) fields[0]=fields[0].substr(3);
            map.assign(fields.size(),-1);
            for(size_t i=0;i<fields.size();++i) {
                std::string lower=fields[i]; for(auto& c:lower) c=char(tolower((unsigned char)c));
                if(lower.rfind("time",0)==0) timeColumn=int(i); else map[i]=FindMonitorChannel(fields[i]);
            }
            for(int signal:REQUIRED) if(std::find(map.begin(),map.end(),signal)==map.end())
                report.missingColumns.push_back(MonitorColumnName(signal));
            if(timeColumn<0) report.missingColumns.insert(report.missingColumns.begin(),"Time(s)");
            if(!report.missingColumns.empty()) { fclose(f); report.error="Log is missing channels autotune needs"; return false; }
            header=false; continue;
        }
        if(fields.size()==1 && fields[0].empty()) continue;
        ++report.rows;
        if(fields.size()!=map.size()) { ++report.skippedRows; continue; }
        char* end=nullptr; const float time=strtof(fields[timeColumn].c_str(),&end);
        if(end==fields[timeColumn].c_str() || !std::isfinite(time)) { ++report.skippedRows; continue; }
        AutoTuneLogFrame frame{time,MonitorData{}};
        frame.data.warmup=frame.data.afterstart=frame.data.accel=100;
        for(size_t i=0;i<fields.size();++i) if(map[i]>=0) Assign(map[i],strtof(fields[i].c_str(),nullptr),frame.data);
        auto& m=frame.data;
        if(m.cellRpm<0 || m.cellRpm>14 || m.cellLoad<0 || m.cellLoad>14) { ++report.skippedRows; continue; }
        m.valid=true;
        if(std::find(report.generations.begin(),report.generations.end(),m.generation)==report.generations.end())
            report.generations.push_back(m.generation);
        frames.push_back(frame);
    }
    fclose(f);
    if(header) { report.error="Log is empty"; return false; }
    if(frames.empty()) { report.error="Log contains no usable rows"; return false; }
    return true;
}
void ReplayAutotuneLog(AutoTune& tune,const std::vector<AutoTuneLogFrame>& frames,
    const AutoTuneContext& ctx,const unsigned char* cal,AutoTuneLogReport& report) {
    const bool collecting=tune.collecting;
    tune.collecting=true; tune.resetHistory();
    report.runningRows=report.veMismatchRows=0;
    for(const auto& frame:frames) {
        const auto& m=frame.data;
        if(cal && (m.state&4) && !(m.state&2)) {
            const double x=m.rpmFraction,y=m.loadFraction; const int at=m.cellLoad*16+m.cellRpm;
            const double ve=cal[at]*(1-x)*(1-y)+cal[at+1]*x*(1-y)+cal[at+16]*(1-x)*y+cal[at+17]*x*y;
            ++report.runningRows;
            if(std::abs(ve-m.ve)>2) ++report.veMismatchRows;
        }
        tune.sample(m,frame.time,ctx);
    }
    tune.resetHistory(); tune.collecting=collecting;
}
