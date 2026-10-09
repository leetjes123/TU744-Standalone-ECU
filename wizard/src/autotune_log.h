#pragma once
#include "autotune.h"
#include <string>
#include <vector>
// Offline autotune: replays a CSV written by Live logging through the same
// AutoTune engine, using the same column names (monitor_channels).
struct AutoTuneLogFrame { float time; MonitorData data; };
struct AutoTuneLogReport {
    std::string path, error;
    int rows=0, skippedRows=0, runningRows=0, veMismatchRows=0;
    std::vector<uint16_t> generations;     // distinct calibration generations in the log
    std::vector<std::string> missingColumns;
};
bool LoadAutotuneLog(const char* path,std::vector<AutoTuneLogFrame>& frames,AutoTuneLogReport& report);
// Runs every frame through tune (collection forced on) and checks the logged
// VE against the table in cal, interpolated at the logged cell and weights.
void ReplayAutotuneLog(AutoTune& tune,const std::vector<AutoTuneLogFrame>& frames,
    const AutoTuneContext& ctx,const unsigned char* cal,AutoTuneLogReport& report);
