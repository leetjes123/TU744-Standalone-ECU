#pragma once
#include "imgui.h"
#include <string>

// All loggable signals from MonitorData
enum LogSignal {
    SIG_RPM,
    SIG_MAP,
    SIG_TPS,
    SIG_AFR,
    SIG_TARGET_AFR,
    SIG_TIMING,
    SIG_VE,
    SIG_CLT,
    SIG_IAT,
    SIG_VBATT,
    SIG_INJ_PW,
    SIG_STFT,
    SIG_OXYGEN,
    SIG_IAC_ACTUAL,
    SIG_TARGET_IDLE,
    SIG_SPEED,
    SIG_GEAR,
    SIG_WARMUP_ENRICH,
    SIG_ASE,
    SIG_ACCEL_ENRICH,
    SIG_SYNC,
    SIG_SYNC_LOSS,
    SIG_CELL_RPM,
    SIG_RPM_FRACTION,
    SIG_CELL_LOAD,
    SIG_LOAD_FRACTION,
    SIG_STATE,
    SIG_FLAGS,
    SIG_INHIBITS,
    SIG_GENERATION,
    SIG_NB_BAND,
    SIG_AFR_VALID,
    SIG_INJ_DUTY,
    SIG_KNOCK_MV, SIG_KNOCK_DETECTED, SIG_KNOCK_RETARD, SIG_KNOCK_VALID, SIG_KNOCK_FAULT,
    SIG_COUNT
};

struct SignalDef {
    const char* name;
    const char* unit;
    ImU32       color;
    float       defaultMin;
    float       defaultMax;
    const char* id;
    const char* shortName;
    const char* aliases; // pipe-separated legacy labels
    const char* description;
    bool advanced = false;
};

extern const SignalDef SIGNAL_DEFS[SIG_COUNT];

int FindMonitorChannel(const std::string& name);
bool MonitorChannelMatches(const std::string& name,const char* query);
std::string MonitorColumnName(int signal);
std::string MonitorShortLabel(const std::string& name);
