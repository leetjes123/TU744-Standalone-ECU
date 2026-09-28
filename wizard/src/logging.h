#pragma once
#include "protocol.h"
#include "imgui.h"
#include <vector>
#include <string>

// Maximum number of samples in the ring buffer (~10 minutes at 20Hz)
static const int LOG_MAX_SAMPLES = 12000;

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
    SIG_COUNT
};

struct SignalDef {
    const char* name;
    const char* unit;
    ImU32       color;
    float       defaultMin;
    float       defaultMax;
};

extern const SignalDef SIGNAL_DEFS[SIG_COUNT];

struct LogState {
    // Signal selection (which signals to plot)
    bool        enabled[SIG_COUNT] = {};

    // Ring buffer for all signals
    float       data[SIG_COUNT][LOG_MAX_SAMPLES] = {};
    int         writeIdx = 0;
    int         sampleCount = 0;

    // Graph settings
    float       graphHeight = 200.0f;   // unscaled
    float       timeWindowSec = 30.0f;  // visible time window
    bool        autoScale = true;
    float       customMin[SIG_COUNT] = {};
    float       customMax[SIG_COUNT] = {};
    bool        stackGraphs = true;     // stacked by default to avoid incompatible Y scales
    bool        groupByUnit = true;     // combine at most two compatible signals per panel

    // Recording
    bool        recording = false;
    FILE*       csvFile = nullptr;
    char        csvPath[260] = {};
    int         csvRows = 0;
    float       recordingElapsed = 0.0f;
    char        csvError[160] = {};

    // Playback rate tracking
    float       sampleTimer = 0.0f;
    float       sampleInterval = 0.05f; // 20Hz sampling

    void init();
    void pushSample(const MonitorData& mon);
    void clear();
    void startRecording();
    void stopRecording();
    void writeCsvHeader();
    void writeCsvRow(const MonitorData& mon);

    static float getSignalValue(LogSignal sig, const MonitorData& mon);
};

// Draw the logging tab UI
void DrawLoggingTab(LogState& log, const MonitorData& mon, bool connected,
                    bool monitoring, float dataAgeSeconds);
