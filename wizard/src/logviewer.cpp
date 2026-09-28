#include "logviewer.h"
#include "logging.h"    // SIGNAL_DEFS / SIG_COUNT for color matching
#include "app.h"        // S() DPI-scale macro
#include "ui_helpers.h"
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

static float NiceStep(float range) {
    float raw  = range / 5.0f;
    float mag  = powf(10.0f, floorf(log10f(fabsf(raw) + 1e-9f)));
    float norm = raw / mag;
    float nice;
    if      (norm <= 1.0f) nice = 1.0f;
    else if (norm <= 2.0f) nice = 2.0f;
    else if (norm <= 5.0f) nice = 5.0f;
    else                   nice = 10.0f;
    return nice * mag;
}

static int FindNearestSample(const std::vector<float>& tv, float t) {
    int n = (int)tv.size();
    if (n == 0) return -1;
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (tv[mid] < t) lo = mid + 1;
        else             hi = mid;
    }
    if (lo == 0) return 0;
    if (lo >= n) return n - 1;
    return (fabsf(tv[lo - 1] - t) <= fabsf(tv[lo] - t)) ? lo - 1 : lo;
}

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
    std::string name = colName;
    size_t p = name.find('(');
    if (p != std::string::npos) {
        while (p > 0 && name[p - 1] == ' ') --p;
        name = name.substr(0, p);
    }
    for (int i = 0; i < SIG_COUNT; i++) {
        if (name == SIGNAL_DEFS[i].name)
            return SIGNAL_DEFS[i].color;
    }
    return s_palette[paletteIdx % s_paletteSize];
}

static std::string GetColumnUnit(const std::string& colName) {
    const size_t open = colName.rfind('(');
    const size_t close = colName.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open + 1) return "unitless";
    return colName.substr(open + 1, close - open - 1);
}

static bool ContainsInsensitive(const std::string& text, const char* query) {
    if (!query || !query[0]) return true;
    const size_t n = strlen(query);
    for (size_t i = 0; i + n <= text.size(); ++i)
        if (_strnicmp(text.c_str() + i, query, n) == 0) return true;
    return false;
}

static void AssignSignalToCompatiblePlot(LogViewerState& lv, int signalIndex) {
    LVSignal& signal = lv.signals[signalIndex];
    const std::string unit = GetColumnUnit(lv.log.columnNames[signal.colIndex]);
    int target = -1;
    for (int p = 0; p < (int)lv.plots.size(); ++p) {
        int members = 0;
        std::string plotUnit;
        for (int i = 0; i < (int)lv.signals.size(); ++i) {
            const LVSignal& member = lv.signals[i];
            if (i == signalIndex || !member.visible || member.plotIndex != p) continue;
            ++members;
            plotUnit = GetColumnUnit(lv.log.columnNames[member.colIndex]);
        }
        const bool compatible = members == 0 || (unit != "unitless" && plotUnit == unit);
        if (members < 2 && compatible) { target = p; break; }
    }
    if (target < 0) {
        LVPlot plot = {};
        snprintf(plot.label, sizeof(plot.label), "%s", unit.c_str());
        lv.plots.push_back(plot);
        target = (int)lv.plots.size() - 1;
    }
    signal.plotIndex = target;
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
                if (!ParseLogNumber(fields[colIdx], parsed[colIdx])) { rowValid = false; break; }
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

    static const char* kDefaults[] = { "RPM", "MAP", "TPS", "AFR", nullptr };
    for (auto& sig : lv.signals) {
        const std::string& name = out.columnNames[sig.colIndex];
        for (int d = 0; kDefaults[d]; d++)
            if (name.find(kDefaults[d]) == 0) { sig.visible = true; break; }
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

// ============================================================
//  DrawLVPlot — draw one plot panel
//  Layout: one y-axis on the left (signal 0), rest on the right.
//  showXAxis: draw time tick labels below (only the last plot).
// ============================================================

static void DrawLVPlot(LogViewerState& lv, int plotIdx, float width, float height, bool showXAxis) {
    if (width < S(50) || height < S(50)) {
        ImGui::Dummy(ImVec2(width, height));
        return;
    }

    ImVec2 pos = ImGui::GetCursorScreenPos();

    char btnId[32];
    snprintf(btnId, sizeof(btnId), "##lvplot%d", plotIdx);
    ImGui::InvisibleButton(btnId, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Collect visible signals for this plot
    static std::vector<int> s_visIdx;
    s_visIdx.clear();
    for (int i = 0; i < (int)lv.signals.size(); i++)
        if (lv.signals[i].visible && lv.signals[i].plotIndex == plotIdx)
            s_visIdx.push_back(i);

    const int   numAxes      = (int)s_visIdx.size();
    const int   numRightAxes = numAxes > 1 ? numAxes - 1 : 0;
    const float perAxisW     = S(44);   // compact axis column width
    const float leftAxisW    = numAxes > 0 ? perAxisW : 0.0f;
    const float rightAxisW   = (float)numRightAxes * perAxisW;
    const float xAxisH       = showXAxis ? S(22) : 0.0f;
    const float legendH      = S(18);

    float plotX = pos.x + leftAxisW;
    float plotY = pos.y + legendH;
    float plotW = width  - leftAxisW - rightAxisW - S(4);
    float plotH = height - legendH   - xAxisH;

    if (plotW < S(20) || plotH < S(20)) return;

    ImVec2 mp = ImGui::GetIO().MousePos;
    bool inPlot = hovered
               && mp.x >= plotX && mp.x <= plotX + plotW
               && mp.y >= plotY && mp.y <= plotY + plotH;

    // ---- Zoom ----
    if (inPlot) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float vr   = lv.viewEnd - lv.viewStart;
            if (vr < 1e-9f) vr = 1e-9f;
            float frac = (mp.x - plotX) / plotW;
            float pivot = lv.viewStart + frac * vr;
            float nr = vr * (wheel > 0.0f ? 0.75f : 1.33f);
            if (nr < 0.05f)           nr = 0.05f;
            if (nr > lv.log.duration) nr = lv.log.duration;
            lv.viewStart = pivot - frac * nr;
            lv.viewEnd   = lv.viewStart + nr;
            if (lv.viewStart < 0.0f) { lv.viewEnd -= lv.viewStart; lv.viewStart = 0.0f; }
            if (lv.viewEnd > lv.log.duration) {
                float over = lv.viewEnd - lv.log.duration;
                lv.viewEnd   = lv.log.duration;
                lv.viewStart = (std::max)(0.0f, lv.viewStart - over);
            }
        }
    }

    // ---- Pan — track per-plot so sibling plots don't reset this state ----
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, S(2))) {
        if (lv.panPlotIdx != plotIdx) {
            lv.panPlotIdx        = plotIdx;
            lv.panStartMouseX    = mp.x;
            lv.panStartViewStart = lv.viewStart;
            lv.panStartViewEnd   = lv.viewEnd;
        }
        float range  = lv.panStartViewEnd - lv.panStartViewStart;
        float timeDt = -(mp.x - lv.panStartMouseX) / plotW * range;
        float ns = lv.panStartViewStart + timeDt;
        float ne = lv.panStartViewEnd   + timeDt;
        if (ns < 0.0f)            { ne -= ns; ns = 0.0f; }
        if (ne > lv.log.duration) { float over = ne - lv.log.duration; ns -= over; ne = lv.log.duration; }
        lv.viewStart = (std::max)(0.0f, ns);
        lv.viewEnd   = (std::min)(lv.log.duration, ne);
    } else if (!active && lv.panPlotIdx == plotIdx) {
        lv.panPlotIdx = -1;
    }

    // ---- Cursor ----
    if (inPlot || active) {
        float vr = lv.viewEnd - lv.viewStart;
        if (vr < 1e-9f) vr = 1e-9f;
        float frac = (mp.x - plotX) / plotW;
        frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
        lv.cursorTime = lv.viewStart + frac * vr;
    }

    float viewRange = lv.viewEnd - lv.viewStart;
    if (viewRange < 1e-9f) viewRange = 1e-9f;
    float timeScale = plotW / viewRange;
    float timeOff   = plotX - lv.viewStart * timeScale;

    // ---- Backgrounds ----
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                      IM_COL32(18, 18, 26, 255), S(4));
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                IM_COL32(45, 45, 65, 255), S(4));
    dl->AddRectFilled(ImVec2(plotX, plotY), ImVec2(plotX + plotW, plotY + plotH),
                      IM_COL32(10, 10, 18, 255));

    // ---- Sample range ----
    int iStart = FindNearestSample(lv.log.time, lv.viewStart);
    int iEnd   = FindNearestSample(lv.log.time, lv.viewEnd);
    if (iStart > iEnd) std::swap(iStart, iEnd);
    iEnd = (std::min)(iEnd + 1, lv.log.sampleCount - 1);

    // ---- Auto-scale Y per signal ----
    for (int vi : s_visIdx) {
        LVSignal& sig = lv.signals[vi];
        float lo = 1e9f, hi = -1e9f;
        for (int s = iStart; s <= iEnd; s++) {
            float v = lv.log.columns[sig.colIndex][s];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (lo > hi) { lo = sig.fullMin; hi = sig.fullMax; }
        float range = hi - lo;
        if (range < 1e-6f) { lo -= 0.5f; hi += 0.5f; range = 1.0f; }
        float pad  = range * 0.05f;
        sig.yMin   = lo - pad;
        sig.yMax   = hi + pad;
    }

    // ---- X grid + optional tick labels ----
    {
        float xStep = NiceStep(viewRange);
        if (xStep < 1e-9f) xStep = 1.0f;
        float xStart = ceilf(lv.viewStart / xStep) * xStep;
        for (float t = xStart; t <= lv.viewEnd + xStep * 0.01f; t += xStep) {
            float x = timeOff + t * timeScale;
            if (x < plotX - 1.0f || x > plotX + plotW + 1.0f) continue;
            dl->AddLine(ImVec2(x, plotY), ImVec2(x, plotY + plotH),
                        IM_COL32(35, 35, 50, 100));
            if (showXAxis) {
                dl->AddLine(ImVec2(x, plotY + plotH), ImVec2(x, plotY + plotH + S(3)),
                            IM_COL32(70, 70, 90, 200));
                char tb[20];
                if (xStep >= 1.0f) snprintf(tb, sizeof(tb), "%.0fs", t);
                else               snprintf(tb, sizeof(tb), "%.2fs", t);
                ImVec2 ts = ImGui::CalcTextSize(tb);
                dl->AddText(ImVec2(x - ts.x * 0.5f, plotY + plotH + S(4)),
                            IM_COL32(100, 100, 130, 220), tb);
            }
        }
    }

    // ---- Left y-axis: first visible signal ----
    if (numAxes > 0) {
        const LVSignal& sig = lv.signals[s_visIdx[0]];
        float axX    = plotX;
        float yRange = sig.yMax - sig.yMin;
        if (yRange < 1e-6f) yRange = 1.0f;
        float yStep  = NiceStep(yRange);
        if (yStep < 1e-9f) yStep = 1.0f;
        float yStart = ceilf(sig.yMin / yStep) * yStep;

        dl->AddLine(ImVec2(axX, plotY), ImVec2(axX, plotY + plotH), sig.color, S(1.5f));

        for (float yv = yStart; yv <= sig.yMax + yStep * 0.01f; yv += yStep) {
            float f = (yv - sig.yMin) / yRange;
            if (f < -0.01f || f > 1.01f) continue;
            float y = plotY + plotH * (1.0f - f);

            // grid (from the primary axis only)
            dl->AddLine(ImVec2(plotX, y), ImVec2(plotX + plotW, y),
                        IM_COL32(35, 35, 50, 100));
            // tick (leftward)
            dl->AddLine(ImVec2(axX - S(4), y), ImVec2(axX, y), sig.color, 1.0f);
            // label
            char lb[20];
            snprintf(lb, sizeof(lb), yStep >= 1.0f ? "%.0f" : "%.2f", yv);
            ImVec2 ts = ImGui::CalcTextSize(lb);
            float lx = axX - ts.x - S(5);
            if (lx < pos.x + S(2)) lx = pos.x + S(2);
            dl->AddText(ImVec2(lx, y - ts.y * 0.5f), sig.color, lb);
        }
    }

    // ---- Right y-axes: signals 1..N-1 ----
    for (int ri = 0; ri < numRightAxes; ri++) {
        const LVSignal& sig = lv.signals[s_visIdx[ri + 1]];
        float axX    = plotX + plotW + (float)ri * perAxisW;
        float yRange = sig.yMax - sig.yMin;
        if (yRange < 1e-6f) yRange = 1.0f;
        float yStep  = NiceStep(yRange);
        if (yStep < 1e-9f) yStep = 1.0f;
        float yStart = ceilf(sig.yMin / yStep) * yStep;

        dl->AddLine(ImVec2(axX, plotY), ImVec2(axX, plotY + plotH), sig.color, S(1.5f));

        for (float yv = yStart; yv <= sig.yMax + yStep * 0.01f; yv += yStep) {
            float f = (yv - sig.yMin) / yRange;
            if (f < -0.01f || f > 1.01f) continue;
            float y = plotY + plotH * (1.0f - f);

            // tick (rightward)
            dl->AddLine(ImVec2(axX, y), ImVec2(axX + S(4), y), sig.color, 1.0f);
            // label (to the right of the axis line)
            char lb[20];
            snprintf(lb, sizeof(lb), yStep >= 1.0f ? "%.0f" : "%.2f", yv);
            ImVec2 ts = ImGui::CalcTextSize(lb);
            float lx = axX + S(6);
            float zoneRight = axX + perAxisW - S(2);
            if (lx + ts.x > zoneRight) lx = zoneRight - ts.x;
            dl->AddText(ImVec2(lx, y - ts.y * 0.5f), sig.color, lb);
        }
    }

    // ---- Signal name above left axis (clipped to axis zone) ----
    if (numAxes > 0) {
        const LVSignal& sig = lv.signals[s_visIdx[0]];
        dl->PushClipRect(ImVec2(pos.x, pos.y), ImVec2(plotX, plotY), true);
        dl->AddText(ImVec2(pos.x + S(2), pos.y + S(3)),
                    sig.color, lv.log.columnNames[sig.colIndex].c_str());
        dl->PopClipRect();
    }
    // ---- Signal names above each right axis ----
    for (int ri = 0; ri < numRightAxes; ri++) {
        const LVSignal& sig = lv.signals[s_visIdx[ri + 1]];
        float axLeft  = plotX + plotW + (float)ri * perAxisW;
        float axRight = axLeft + perAxisW;
        dl->PushClipRect(ImVec2(axLeft, pos.y), ImVec2(axRight, plotY), true);
        dl->AddText(ImVec2(axLeft + S(2), pos.y + S(3)),
                    sig.color, lv.log.columnNames[sig.colIndex].c_str());
        dl->PopClipRect();
    }

    // ---- Plot border ----
    dl->AddLine(ImVec2(plotX, plotY), ImVec2(plotX, plotY + plotH),
                IM_COL32(60, 60, 80, 200));
    dl->AddLine(ImVec2(plotX, plotY + plotH), ImVec2(plotX + plotW, plotY + plotH),
                IM_COL32(60, 60, 80, 200));

    // ---- Signal polylines ----
    dl->PushClipRect(ImVec2(plotX, plotY), ImVec2(plotX + plotW, plotY + plotH), true);

    static std::vector<ImVec2> s_pts;

    for (int vi : s_visIdx) {
        const LVSignal& sig = lv.signals[vi];
        float yRange = sig.yMax - sig.yMin;
        if (yRange < 1e-9f) yRange = 1.0f;
        float invRange = 1.0f / yRange;

        s_pts.clear();
        s_pts.reserve(iEnd - iStart + 2);

        for (int s = iStart; s <= iEnd; s++) {
            float x = timeOff + lv.log.time[s] * timeScale;
            float v = lv.log.columns[sig.colIndex][s];
            float yFrac = (v - sig.yMin) * invRange;
            yFrac = yFrac < 0.0f ? 0.0f : (yFrac > 1.0f ? 1.0f : yFrac);
            s_pts.push_back(ImVec2(x, plotY + plotH * (1.0f - yFrac)));
        }

        if ((int)s_pts.size() >= 2)
            dl->AddPolyline(s_pts.data(), (int)s_pts.size(),
                            sig.color, ImDrawFlags_None, S(1.5f));
    }

    // ---- Cursor line ----
    if (lv.cursorTime >= lv.viewStart - 0.001f && lv.cursorTime <= lv.viewEnd + 0.001f) {
        float cx = timeOff + lv.cursorTime * timeScale;
        dl->AddLine(ImVec2(cx, plotY), ImVec2(cx, plotY + plotH),
                    IM_COL32(255, 255, 100, 180), S(1.0f));
    }

    dl->PopClipRect();

    // ---- Cursor time label (last plot only) ----
    if (showXAxis && lv.cursorTime >= lv.viewStart && lv.cursorTime <= lv.viewEnd) {
        float cx = timeOff + lv.cursorTime * timeScale;
        char ctb[24];
        snprintf(ctb, sizeof(ctb), "%.3fs", lv.cursorTime);
        ImVec2 ts = ImGui::CalcTextSize(ctb);
        float tx = cx - ts.x * 0.5f;
        if (tx < plotX)                tx = plotX;
        if (tx + ts.x > plotX + plotW) tx = plotX + plotW - ts.x;
        dl->AddText(ImVec2(tx, plotY + plotH + S(4)),
                    IM_COL32(255, 255, 100, 255), ctb);
    }

    // ---- Value tooltip ----
    if ((hovered || active) && !s_visIdx.empty() && lv.log.sampleCount > 0) {
        int nearIdx = FindNearestSample(lv.log.time, lv.cursorTime);
        if (nearIdx >= 0) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f),
                               "t = %.3f s", lv.log.time[nearIdx]);
            ImGui::Separator();
            for (int vi : s_visIdx) {
                const LVSignal& sig   = lv.signals[vi];
                const std::string& cn = lv.log.columnNames[sig.colIndex];
                float val             = lv.log.columns[sig.colIndex][nearIdx];
                ImVec4 col4 = ImGui::ColorConvertU32ToFloat4(sig.color);
                ImGui::TextColored(col4, "%-22s", cn.c_str());
                ImGui::SameLine(0, S(4));
                char vbuf[20];
                if (fabsf(val) >= 1000.0f)     snprintf(vbuf, sizeof(vbuf), "%.0f",  val);
                else if (fabsf(val) >= 10.0f)  snprintf(vbuf, sizeof(vbuf), "%.1f",  val);
                else if (fabsf(val) >= 0.01f)  snprintf(vbuf, sizeof(vbuf), "%.3f",  val);
                else                           snprintf(vbuf, sizeof(vbuf), "%.4g",  val);
                ImGui::TextUnformatted(vbuf);
            }
            ImGui::EndTooltip();
        }
    }
}

// ============================================================
//  DrawLogViewer  — top-level tab contents
// ============================================================

void DrawLogViewer(LogViewerState& lv) {
    if (!lv.parseOk || lv.log.sampleCount < 2) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.3f, 0.3f, 1.0f),
            "Log could not be loaded or contains fewer than 2 samples.");
        return;
    }

    float avail      = ImGui::GetContentRegionAvail().x;
    float sidebarW   = S(220);
    float graphAreaW = avail - sidebarW - S(8);

    // ============================================================
    //  Left panel: signal selector
    // ============================================================
    ImGui::BeginChild("##lvsig", ImVec2(sidebarW, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Log Signals");
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
                       "%.1f s  |  %d samples", lv.log.duration, lv.log.sampleCount);
    if (lv.log.skippedRows > 0) {
        ImGui::TextColored(ImVec4(0.93f, 0.67f, 0.20f, 1.0f),
                           "Import: %d/%d rows skipped", lv.log.skippedRows, lv.log.totalDataRows);
        if (ImGui::TreeNode("Parsing diagnostics")) {
            for (const std::string& detail : lv.log.diagnostics) ImGui::BulletText("%s", detail.c_str());
            if (lv.log.skippedRows > (int)lv.log.diagnostics.size())
                ImGui::TextDisabled("%d more skipped rows", lv.log.skippedRows - (int)lv.log.diagnostics.size());
            ImGui::TreePop();
        }
    }
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-S(6));
    ImGui::InputTextWithHint("##signal_filter", "Search signals...", lv.signalFilter,
                             sizeof(lv.signalFilter));

    if (ImGui::SmallButton("Fit All")) {
        lv.viewStart = 0.0f;
        lv.viewEnd   = lv.log.duration;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Show filtered")) {
        for (int i = 0; i < (int)lv.signals.size(); ++i) {
            LVSignal& signal = lv.signals[i];
            if (!signal.visible && ContainsInsensitive(lv.log.columnNames[signal.colIndex], lv.signalFilter)) {
                signal.visible = true;
                AssignSignalToCompatiblePlot(lv, i);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Hide filtered")) {
        for (auto& s : lv.signals)
            if (ContainsInsensitive(lv.log.columnNames[s.colIndex], lv.signalFilter)) s.visible = false;
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ---- Plot management ----
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "Plots");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Plot")) {
        LVPlot np;
        snprintf(np.label, sizeof(np.label), "Plot %d", (int)lv.plots.size() + 1);
        lv.plots.push_back(np);
    }
    if ((int)lv.plots.size() > 1) {
        ImGui::SameLine();
        int lastPlotMembers = 0;
        for (const auto& signal : lv.signals)
            if (signal.visible && signal.plotIndex == (int)lv.plots.size() - 1) ++lastPlotMembers;
        if (lastPlotMembers > 0) ImGui::BeginDisabled();
        if (ImGui::SmallButton("- Plot")) {
            lv.plots.pop_back();
            int maxIdx = (int)lv.plots.size() - 1;
            for (auto& sig : lv.signals)
                if (sig.plotIndex > maxIdx)
                    sig.plotIndex = maxIdx;
        }
        if (lastPlotMembers > 0) ImGui::EndDisabled();
        if (lastPlotMembers > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Hide or move signals out of the last plot before removing it.");
    }

    for (int p = 0; p < (int)lv.plots.size(); ++p) {
        ImGui::PushID(5000 + p);
        ImGui::SetNextItemWidth(-S(6));
        ImGui::InputText("##plot_name", lv.plots[p].label, sizeof(lv.plots[p].label));
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    int numPlots = (int)lv.plots.size();

    // ---- Signal list ----
    int matchedSignals = 0;
    for (int i = 0; i < (int)lv.signals.size(); i++) {
        LVSignal& sig = lv.signals[i];
        const std::string& cname = lv.log.columnNames[sig.colIndex];
        if (!ContainsInsensitive(cname, lv.signalFilter)) continue;
        ++matchedSignals;

        // Color swatch
        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float swSz = S(10);
        ImU32 swCol = sig.visible ? sig.color : IM_COL32(50, 50, 60, 255);
        dl->AddRectFilled(ImVec2(cp.x, cp.y + S(3)),
                          ImVec2(cp.x + swSz, cp.y + S(13)), swCol);
        ImGui::Dummy(ImVec2(swSz + S(4), 0));
        ImGui::SameLine(0, 0);

        char chkId[128];
        snprintf(chkId, sizeof(chkId), "%s##sig%d", cname.c_str(), i);
        if (ImGui::Checkbox(chkId, &sig.visible) && sig.visible)
            AssignSignalToCompatiblePlot(lv, i);

        if (sig.visible) {
            // Plot assignment buttons — only shown when multiple plots exist
            if (numPlots > 1) {
                for (int p = 0; p < numPlots; p++) {
                    ImGui::SameLine(0, p == 0 ? S(4) : S(2));
                    char btnLbl[12];
                    snprintf(btnLbl, sizeof(btnLbl), "P%d##p%d_%d", p + 1, i, p);
                    bool isCur = (sig.plotIndex == p);
                    int plotMembers = 0;
                    for (const auto& member : lv.signals)
                        if (member.visible && member.plotIndex == p) ++plotMembers;
                    const bool full = !isCur && plotMembers >= 2;
                    if (isCur) {
                        ImVec4 c = ImGui::ColorConvertU32ToFloat4(sig.color);
                        ImGui::PushStyleColor(ImGuiCol_Button, c);
                        ImGui::PushStyleColor(ImGuiCol_Text,   ImVec4(0, 0, 0, 1));
                    }
                    if (full) ImGui::BeginDisabled();
                    if (ImGui::SmallButton(btnLbl)) sig.plotIndex = p;
                    if (full) ImGui::EndDisabled();
                    if (isCur) ImGui::PopStyleColor(2);
                }
            }

            // Current value at cursor
            int nearIdx = FindNearestSample(lv.log.time, lv.cursorTime);
            if (nearIdx >= 0) {
                float val = lv.log.columns[sig.colIndex][nearIdx];
                ImGui::SameLine();
                char vbuf[20];
                if (fabsf(val) >= 1000.0f)     snprintf(vbuf, sizeof(vbuf), "%.0f",  val);
                else if (fabsf(val) >= 10.0f)  snprintf(vbuf, sizeof(vbuf), "%.1f",  val);
                else if (fabsf(val) >= 0.01f)  snprintf(vbuf, sizeof(vbuf), "%.3f",  val);
                else                           snprintf(vbuf, sizeof(vbuf), "%.4g",  val);
                ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.75f, 1.0f), "%s", vbuf);
            }

            // Full-log range
            char rb[40];
            snprintf(rb, sizeof(rb), "  %.2g \xe2\x80\x93 %.2g", sig.fullMin, sig.fullMax);
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.5f, 1.0f), "%s", rb);
        }
    }
    if (matchedSignals == 0) ImGui::TextDisabled("No signals match this filter.");

    ImGui::EndChild();
    ImGui::SameLine();

    // ============================================================
    //  Right panel: stacked plot panels
    // ============================================================
    ImGui::BeginChild("##lvgrapharea", ImVec2(graphAreaW, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    {
        int visibleCount = 0;
        for (const auto& signal : lv.signals) if (signal.visible) ++visibleCount;
        const int cursorIndex = FindNearestSample(lv.log.time, lv.cursorTime);
        const float readoutH = visibleCount > 0 ? S(74) : 0.0f;
        if (visibleCount > 0 && ImGui::BeginChild("##cursor_readout", ImVec2(0, readoutH), ImGuiChildFlags_Borders)) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "Cursor %.3f s", lv.cursorTime);
            if (ImGui::BeginTable("##cursor_values", 4, ImGuiTableFlags_SizingStretchSame)) {
                for (const auto& signal : lv.signals) {
                    if (!signal.visible || cursorIndex < 0) continue;
                    ImGui::TableNextColumn();
                    const std::string& name = lv.log.columnNames[signal.colIndex];
                    const float value = lv.log.columns[signal.colIndex][cursorIndex];
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(signal.color),
                                       "%s  %g", name.c_str(), value);
                }
                ImGui::EndTable();
            }
        }
        if (visibleCount > 0) ImGui::EndChild();

        float totalH = ImGui::GetContentRegionAvail().y - S(4);
        int   np     = (int)lv.plots.size();
        float gap    = S(4);
        float ph     = (totalH - gap * (float)(np - 1)) / (float)np;
        if (ph < S(80)) ph = S(80);

        for (int p = 0; p < np; p++) {
            DrawLVPlot(lv, p, ScrollSafeWidth(ImGui::GetContentRegionAvail().x), ph, p == np - 1);
            if (p < np - 1)
                ImGui::Dummy(ImVec2(1.0f, gap));
        }
    }
    ImGui::EndChild();
}
