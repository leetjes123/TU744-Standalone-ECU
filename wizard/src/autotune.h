#pragma once
#include "protocol.h"
#include "calibration.h"
#include <array>
#include <deque>
// Collect suggestions only; applying them edits the local VE map. The normal
// verified command-32 path sends them. Persistence is always a separate action.
struct AutoTune {
    bool collecting=false;
    float delaySeconds=0.2f;
    struct Sample { float time; MonitorData data; bool steady; };
    struct Cell { double error=0, weight=0; unsigned samples=0; };
    std::array<Cell,256> cells{};
    std::deque<Sample> history;
    const char* status="Select wideband input and turn closed-loop trim off";
    void clear() { cells={}; history.clear(); }
    void sample(const MonitorData& m,float time,bool wideband);
    int propose(const CalBuffer& source,CalBuffer& result) const;
};
