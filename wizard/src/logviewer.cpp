#include "logviewer.h"
#include "logging.h"    // SIGNAL_DEFS / SIG_COUNT for color matching
#include "app.h"        // S() DPI-scale macro
#include "ui_helpers.h"
#include "graph_math.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// ============================================================
//  Helpers
// ============================================================

// ============================================================
//  Color assignment
// ============================================================

static const ImU32 s_palette[] = {
    IM_COL32(255,  90,  90, 255),
    IM_COL32( 90, 150, 255, 255),
    IM_COL32(255, 200,  50, 255),
    IM_COL32( 90, 255, 120, 255),
    IM_COL32(200, 100, 255, 255),
    IM_COL32(255, 150,  50, 255),
    IM_COL32( 50, 230, 255, 255),
    IM_COL32(255, 100, 150, 255),
    IM_COL32(180, 255, 100, 255),
    IM_COL32(200, 200, 100, 255),
};
static const int s_paletteSize = (int)(sizeof(s_palette) / sizeof(s_palette[0]));

static ImU32 GetColumnColor(const std::string& colName, int paletteIdx) {
    const int channel=FindMonitorChannel(colName);
    if(channel>=0) return SIGNAL_DEFS[channel].color;
    return s_palette[paletteIdx % s_paletteSize];
}

static std::string GetColumnUnit(const std::string& colName) {
    const size_t open = colName.rfind('(');
    const size_t close = colName.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open + 1) return "unitless";
    return colName.substr(open + 1, close - open - 1);
}

// ============================================================
//  ParseLogFile
// ============================================================

static bool ParseCsvLine(const char* line, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool quoted = false;
    for (const char* p = line; ; ++p) {
        const char ch = *p;
        if (quoted) {
            if (ch == '\0') return false;
            if (ch == '"') {
                if (p[1] == '"') { field.push_back('"'); ++p; }
                else quoted = false;
            } else field.push_back(ch);
        } else if (ch == '"' && field.empty()) {
            quoted = true;
        } else if (ch == ',' || ch == '\0') {
            fields.push_back(field);
            field.clear();
            if (ch == '\0') return true;
        } else {
            field.push_back(ch);
        }
    }
}

static bool ParseLogNumber(const std::string& field, float& value) {
    const char* begin = field.c_str();
    while (std::isspace((unsigned char)*begin)) ++begin;
    errno = 0;
    char* end = nullptr;
    value = strtof(begin, &end);
    if (end == begin || errno == ERANGE || !std::isfinite(value)) return false;
    while (std::isspace((unsigned char)*end)) ++end;
    return *end == '\0';
}

bool ParseLogFile(const char* path, LogViewerState& lv) {
    lv = LogViewerState{};
    ParsedLog& out = lv.log;

    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[8192];
    bool firstLine  = true;
    int  timeColIdx = -1;
    int  numCols    = 0;
    int  lineNumber = 0;

    while (fgets(line, (int)sizeof(line), f)) {
        ++lineNumber;
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;

        if (firstLine) {
            firstLine = false;
            std::vector<std::string> fields;
            if (!ParseCsvLine(line, fields) || fields.empty()) {
                lv.parseError = "Malformed CSV header";
                fclose(f);
                return false;
            }
            for (int colIdx = 0; colIdx < (int)fields.size(); ++colIdx) {
                std::string n = fields[colIdx];
                const size_t first = n.find_first_not_of(" \t");
                const size_t last = n.find_last_not_of(" \t");
                n = first == std::string::npos ? "" : n.substr(first, last - first + 1);
                out.columnNames.push_back(n);
                if (n == "Time(s)" || n == "Time" || n == "time" || n == "TIME")
                    timeColIdx = colIdx;
            }
            numCols = (int)out.columnNames.size();
            out.columns.resize(numCols);
            if (timeColIdx == -1 && numCols > 0)
                timeColIdx = 0;
        } else {
            ++out.totalDataRows;
            std::vector<std::string> fields;
            if (!ParseCsvLine(line, fields) || (int)fields.size() != numCols) {
                ++out.skippedRows;
                if (out.diagnostics.size() < 12) {
                    char detail[128];
                    snprintf(detail, sizeof(detail), "Line %d skipped: column count does not match header", lineNumber);
                    out.diagnostics.push_back(detail);
                }
                continue;
            }
            float timeVal = 0.0f;
            std::vector<float> parsed(numCols, 0.0f);
            bool rowValid = true;
            for (int colIdx = 0; colIdx < numCols; ++colIdx) {
                if (!ParseLogNumber(fields[colIdx], parsed[colIdx])) {
                    const int channel=FindMonitorChannel(out.columnNames[colIdx]);
                    const bool optionalKnock=channel>=SIG_KNOCK_MV && channel<=SIG_KNOCK_FAULT;
                    const bool missing=fields[colIdx].empty() || !_stricmp(fields[colIdx].c_str(),"nan");
                    if(colIdx!=timeColIdx && optionalKnock && missing) parsed[colIdx]=NAN;
                    else { rowValid = false; break; }
                }
                if (colIdx == timeColIdx) timeVal = parsed[colIdx];
            }
            if (rowValid && !out.time.empty() && timeVal <= out.time.back()) rowValid = false;
            if (!rowValid) {
                ++out.skippedRows;
                if (out.diagnostics.size() < 12) {
                    char detail[128];
                    snprintf(detail, sizeof(detail), "Line %d skipped: invalid value or non-increasing time", lineNumber);
                    out.diagnostics.push_back(detail);
                }
                continue;
            }
            for (int colIdx = 0; colIdx < numCols; ++colIdx)
                out.columns[colIdx].push_back(parsed[colIdx]);
            out.time.push_back(timeVal);
        }
    }
    fclose(f);

    out.sampleCount = (int)out.time.size();
    if (out.sampleCount < 2) {
        lv.parseError = "Log contains fewer than two valid data rows";
        return false;
    }

    float t0 = out.time.front();
    if (t0 != 0.0f)
        for (float& t : out.time) t -= t0;
    out.duration = out.time.back();
    out.filePath = path;

    // Preserve imported units and samples while presenting recognized legacy headers consistently.
    for(int c=0;c<numCols;++c) if(c!=timeColIdx) {
        const int channel=FindMonitorChannel(out.columnNames[c]);
        if(channel<0) continue;
        const auto unit=out.columnNames[c].rfind('(');
        const std::string suffix=unit!=std::string::npos?out.columnNames[c].substr(unit):"";
        out.columnNames[c]=std::string(SIGNAL_DEFS[channel].name)+suffix;
    }
    int paletteIdx = 0;
    for (int c = 0; c < numCols; c++) {
        if (c == timeColIdx) continue;

        LVSignal sig;
        sig.colIndex  = c;
        sig.plotIndex = 0;
        sig.visible   = false;
        sig.color     = GetColumnColor(out.columnNames[c], paletteIdx++);

        float lo = 1e9f, hi = -1e9f;
        for (float v : out.columns[c]) {
            if (!GraphValueValid(out.columnNames[c],v)) continue;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (lo > hi) { lo = 0.0f; hi = 1.0f; }
        if (hi - lo < 1e-6f) { lo -= 0.5f; hi += 0.5f; }
        sig.fullMin = lo;
        sig.fullMax = hi;
        sig.yMin    = lo;
        sig.yMax    = hi;
        lv.signals.push_back(sig);
    }

    for (auto& sig : lv.signals) {
        const int channel=FindMonitorChannel(out.columnNames[sig.colIndex]);
        sig.visible=channel==SIG_RPM || channel==SIG_MAP || channel==SIG_TPS ||
                    channel==SIG_AFR || channel==SIG_TARGET_AFR || channel==SIG_STFT;
    }
    {
        int n = 0;
        for (auto& sig : lv.signals) if (sig.visible) n++;
        if (n == 0)
            for (int i = 0; i < (int)lv.signals.size() && i < 4; i++)
                lv.signals[i].visible = true;
    }

    // Default to stacked panels grouped by compatible units, max two signals per plot.
    for (auto& sig : lv.signals) sig.plotIndex = -1;
    for (auto& sig : lv.signals) {
        if (!sig.visible) continue;
        const std::string unit = GetColumnUnit(out.columnNames[sig.colIndex]);
        int target = -1;
        for (int p = 0; p < (int)lv.plots.size(); ++p) {
            int members = 0;
            std::string plotUnit;
            for (const auto& candidate : lv.signals) {
                if (!candidate.visible || candidate.plotIndex != p) continue;
                ++members;
                plotUnit = GetColumnUnit(out.columnNames[candidate.colIndex]);
            }
            if (members < 2 && unit != "unitless" && plotUnit == unit) { target = p; break; }
        }
        if (target < 0) {
            LVPlot plot = {};
            snprintf(plot.label, sizeof(plot.label), "%s", unit.c_str());
            lv.plots.push_back(plot);
            target = (int)lv.plots.size() - 1;
        }
        sig.plotIndex = target;
    }
    if (lv.plots.empty()) {
        LVPlot plot = {};
        snprintf(plot.label, sizeof(plot.label), "Plot 1");
        lv.plots.push_back(plot);
    }
    for (auto& sig : lv.signals) if (sig.plotIndex < 0) sig.plotIndex = 0;

    lv.viewStart  = 0.0f;
    lv.viewEnd    = out.duration;
    lv.cursorTime = 0.0f;
    lv.parseOk    = true;

    const char* slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    lv.tabLabel = slash ? slash + 1 : path;

    return true;
}
