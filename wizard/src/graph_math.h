#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include "monitor_channels.h"

inline void ClampGraphWindow(float duration,float& start,float& end) {
    if(!std::isfinite(duration) || duration<=0) { start=end=0; return; }
    if(!std::isfinite(start) || !std::isfinite(end) || end<=start) { start=0; end=duration; return; }
    const float span=std::clamp(end-start,std::min(.05f,duration),duration);
    start=std::clamp(start,0.0f,duration-span); end=start+span;
}
inline void ZoomGraphWindow(float duration,float pivot,float factor,float& start,float& end) {
    ClampGraphWindow(duration,start,end);
    if(duration<=0 || !std::isfinite(factor) || factor<=0) return;
    if(!std::isfinite(pivot)) pivot=(start+end)*.5f;
    pivot=std::clamp(pivot,start,end);
    const float fraction=std::clamp((pivot-start)/(end-start),0.0f,1.0f);
    const float span=std::clamp((end-start)*factor,std::min(.05f,duration),duration);
    start=pivot-fraction*span; end=start+span;
    ClampGraphWindow(duration,start,end);
}
inline int NearestGraphSample(const std::vector<float>& time,float cursor) {
    if(time.empty()) return -1;
    auto it=std::lower_bound(time.begin(),time.end(),cursor);
    if(it==time.end()) return int(time.size())-1;
    const int index=int(it-time.begin());
    return index && cursor-time[index-1]<=time[index]-cursor?index-1:index;
}
inline bool GraphValueValid(const std::string& name,float value) {
    const bool measured=name=="AFR" || name.rfind("AFR(",0)==0 ||
                        name.rfind("AFR Actual",0)==0 || name.rfind("Measured AFR",0)==0;
    return std::isfinite(value) && (!measured || value>0);
}
struct GraphStatistics {
    int count=0;
    double sum=0;
    float minimum=0,maximum=0;
    void add(float value) {
        if(!count) minimum=maximum=value;
        else { minimum=std::min(minimum,value); maximum=std::max(maximum,value); }
        ++count; sum+=value;
    }
    double average() const { return count?sum/count:0; }
};
inline GraphStatistics SelectedGraphStatistics(const std::vector<float>& time,const std::vector<float>& values,
                                               const std::string& name,float start,float end) {
    GraphStatistics result;
    if(start>end) std::swap(start,end);
    auto first=std::lower_bound(time.begin(),time.end(),start);
    for(auto it=first;it!=time.end() && *it<=end;++it) {
        const size_t index=it-time.begin();
        if(index<values.size() && GraphValueValid(name,values[index])) result.add(values[index]);
    }
    return result;
}
