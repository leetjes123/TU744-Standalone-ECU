#pragma once
#include "protocol.h"
#include "imgui.h"
#include "logviewer.h"
#include "monitor_channels.h"
#include <vector>
#include <string>

// Maximum number of samples in the ring buffer (~10 minutes at 20Hz)
static const int LOG_MAX_SAMPLES = 12000;

struct LogState {
    // Signal selection (which signals to plot)
    bool        enabled[SIG_COUNT] = {};

    // Ring buffer for all signals
    float       data[SIG_COUNT][LOG_MAX_SAMPLES] = {};
    int         writeIdx = 0;
    int         sampleCount = 0;
    double      sampleTimes[LOG_MAX_SAMPLES] = {};
    LogViewerState viewer;
    int viewerWriteIdx = -1, viewerSampleCount = -1;

    // Graph settings
    float       timeWindowSec = 30.0f;  // visible time window

    // Recording
    bool        recording = false;
    FILE*       csvFile = nullptr;
    char        csvPath[260] = {};
    int         csvRows = 0;
    float       recordingElapsed = 0.0f;
    double      recordingBaseTime = -1;
    char        csvError[160] = {};

    void init();
    void pushSample(const MonitorData& mon, double timeSeconds = -1);
    void refreshViewer();
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
