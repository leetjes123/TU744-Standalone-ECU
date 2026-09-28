#pragma once
#include "imgui.h"
#include <vector>
#include <string>

// ============================================================
//  Log Viewer — open and inspect recorded CSV log files
//  (separate from the live logging tab in logging.h)
// ============================================================

struct ParsedLog {
    std::vector<std::string>         columnNames;  // one per CSV column incl. Time
    std::vector<std::vector<float>>  columns;       // columns[colIdx][sampleIdx]
    std::vector<float>               time;          // time axis (seconds, starts at 0)
    int                              sampleCount = 0;
    float                            duration    = 0.0f;
    std::string                      filePath;
    int                              totalDataRows = 0;
    int                              skippedRows = 0;
    std::vector<std::string>         diagnostics;
};

struct LVSignal {
    int    colIndex  = 0;
    int    plotIndex = 0;    // which plot panel this signal belongs to (0-based)
    bool   visible   = false;
    float  yMin      = 0.0f;
    float  yMax      = 1.0f;
    float  fullMin   = 0.0f;
    float  fullMax   = 1.0f;
    ImU32  color     = IM_COL32(200, 200, 200, 255);
};

struct LVPlot {
    char label[32];
};

struct LogViewerState {
    ParsedLog             log;
    std::vector<LVSignal> signals;
    std::vector<LVPlot>   plots;      // one or more plot panels

    // Shared zoom / pan (all plots use the same X axis)
    float  viewStart         = 0.0f;
    float  viewEnd           = 0.0f;

    // Pan drag state (tracks which plot is being panned)
    int    panPlotIdx        = -1;
    float  panStartMouseX    = 0.0f;
    float  panStartViewStart = 0.0f;
    float  panStartViewEnd   = 0.0f;

    // Cursor (hover position on time axis)
    float  cursorTime        = 0.0f;
    char   signalFilter[64]  = {};
    bool   showDiagnostics   = true;

    bool         parseOk  = false;
    std::string  parseError;
    std::string  tabLabel;
};

// Parse a CSV file into a LogViewerState. Returns false on failure.
bool ParseLogFile(const char* path, LogViewerState& lv);

// Draw the log viewer tab contents.
void DrawLogViewer(LogViewerState& lv);
