#include "calibration_safety.h"
#include <algorithm>
std::vector<CalibrationRange> BuildDirtyRanges(const unsigned char* data, const unsigned char* baseline, bool synced) {
    if (!synced) return {{0, CAL_SIZE}};
    std::vector<CalibrationRange> result;
    for (int i=0; i<CAL_SIZE;) {
        if (data[i]==baseline[i]) { ++i; continue; }
        int begin=i++;
        while(i<CAL_SIZE && data[i]!=baseline[i]) ++i;
        result.push_back({begin,i});
    }
    return result;
}
int CountRangeBytes(const std::vector<CalibrationRange>& ranges) {
    int n=0; for (const auto& r:ranges) n+=r.end-r.start; return n;
}
std::vector<CalibrationRange> SplitLiveWrites(const std::vector<CalibrationRange>& ranges) {
    std::vector<CalibrationRange> result;
    for (const auto& r:ranges) {
        if (r.start<0 || r.end>0x400 || r.start>=r.end) return {};
        for (int pos=r.start; pos<r.end;) {
            int end=std::min({r.end, pos+16, ((pos/256)+1)*256});
            result.push_back({pos,end}); pos=end;
        }
    }
    return result;
}
const char* FeatureForOffset(int offset) {
    return IsStructuralOffset(offset) ? "Stopped engine only" : "Calibration";
}
