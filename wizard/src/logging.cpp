#include "logging.h"
#include "app.h"
#include "ui_helpers.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <windows.h>
#include <commdlg.h>
#include <ctime>

// ============================================================
//  Signal definitions: name, unit, color, default range
// ============================================================
const SignalDef SIGNAL_DEFS[SIG_COUNT] = {
    {"RPM","rpm",IM_COL32(80,80,80,255),0,8000},
    {"MAP","kPa",IM_COL32(133,163,111,255),0,300},
    {"TPS","%",IM_COL32(186,246,142,255),0,100},
    {"Measured AFR","AFR",IM_COL32(239,154,173,255),0,25},
    {"Target AFR","AFR",IM_COL32(117,237,204,255),7,22},
    {"Planned advance","deg",IM_COL32(170,145,235,255),-20,50},
    {"VE","%",IM_COL32(223,228,91,255),0,255},
    {"CLT","C",IM_COL32(101,136,122,255),-40,150},
    {"IAT","C",IM_COL32(154,219,153,255),-40,150},
    {"Battery","V",IM_COL32(207,127,184,255),0,20},
    {"Planned injector PW","ms",IM_COL32(85,210,215,255),0,30},
    {"Applied trim","%",IM_COL32(138,118,246,255),-50,50},
    {"Oxygen input","mV",IM_COL32(191,201,102,255),0,1275},
    {"IAC position","steps",IM_COL32(244,109,133,255),0,220},
    {"Idle target","rpm",IM_COL32(122,192,164,255),0,2550},
    {"Speed","km/h",IM_COL32(175,100,195,255),0,255},
    {"Gear","",IM_COL32(228,183,226,255),0,15},
    {"Warm-up","%",IM_COL32(106,91,82,255),100,255},
    {"After-start","%",IM_COL32(159,174,113,255),100,255},
    {"Acceleration","%",IM_COL32(212,82,144,255),100,255},
    {"Sync","",IM_COL32(90,165,175,255),0,1},
    {"Sync losses","count",IM_COL32(143,248,206,255),0,65535},
    {"RPM cell","",IM_COL32(196,156,237,255),0,14},
    {"RPM fraction","",IM_COL32(249,239,93,255),0,1},
    {"Load cell","",IM_COL32(127,147,124,255),0,14},
    {"Load fraction","",IM_COL32(180,230,155,255),0,1},
    {"Engine state bits","",IM_COL32(233,138,186,255),0,255},
    {"Status bits","",IM_COL32(111,221,217,255),0,255},
    {"Output inhibits","",IM_COL32(164,129,248,255),0,65535},
    {"Tune generation","",IM_COL32(217,212,104,255),0,65535},
    {"Narrowband band","",IM_COL32(95,120,135,255),0,4},
    {"AFR valid","",IM_COL32(148,203,166,255),0,1},
    {"Planned injector duty","%",IM_COL32(180,130,255,255),0,100},
};
float LogState::getSignalValue(LogSignal sig,const MonitorData& mon) {
    switch(sig) {
    case SIG_RPM: return float(mon.rpm);
    case SIG_MAP: return float(mon.kpa);
    case SIG_TPS: return float(mon.tps);
    case SIG_AFR: return float(mon.measuredAfr);
    case SIG_TARGET_AFR: return float(mon.targetAfr);
    case SIG_TIMING: return float(mon.advance);
    case SIG_VE: return float(mon.ve);
    case SIG_CLT: return float(mon.clt);
    case SIG_IAT: return float(mon.iat);
    case SIG_VBATT: return float(mon.battery);
    case SIG_INJ_PW: return float(mon.pulseUs*0.001f);
    case SIG_STFT: return float(mon.trimPercent);
    case SIG_OXYGEN: return float(mon.oxygenMv);
    case SIG_IAC_ACTUAL: return float(mon.iacPosition);
    case SIG_TARGET_IDLE: return float(mon.idleTarget);
    case SIG_SPEED: return float(mon.speed);
    case SIG_GEAR: return float(mon.gear);
    case SIG_WARMUP_ENRICH: return float(mon.warmup);
    case SIG_ASE: return float(mon.afterstart);
    case SIG_ACCEL_ENRICH: return float(mon.accel);
    case SIG_SYNC: return float(mon.synced());
    case SIG_SYNC_LOSS: return float(mon.lossOfSyncCount);
    case SIG_CELL_RPM: return float(mon.cellRpm);
    case SIG_RPM_FRACTION: return float(mon.rpmFraction);
    case SIG_CELL_LOAD: return float(mon.cellLoad);
    case SIG_LOAD_FRACTION: return float(mon.loadFraction);
    case SIG_STATE: return float(mon.state);
    case SIG_FLAGS: return float(mon.flags);
    case SIG_INHIBITS: return float(mon.inhibits);
    case SIG_GENERATION: return float(mon.generation);
    case SIG_NB_BAND: return float(mon.narrowbandBand);
    case SIG_AFR_VALID: return float(mon.measuredAfr>0);
    case SIG_INJ_DUTY: return mon.plannedDutyPercent();
    default: return 0;
    }
}
// ============================================================
//  LogState methods
// ============================================================
void LogState::init() {
    // Enable a sensible default set
    stackGraphs = true;
    groupByUnit = true;
    enabled[SIG_RPM]    = true;
    enabled[SIG_MAP]    = true;
    enabled[SIG_TPS]    = true;
    enabled[SIG_AFR]    = true;

    for (int s = 0; s < SIG_COUNT; s++) {
        customMin[s] = SIGNAL_DEFS[s].defaultMin;
        customMax[s] = SIGNAL_DEFS[s].defaultMax;
    }
}

void LogState::pushSample(const MonitorData& mon) {
    for (int s = 0; s < SIG_COUNT; s++)
        data[s][writeIdx] = getSignalValue((LogSignal)s, mon);

    if (sampleCount < LOG_MAX_SAMPLES) sampleCount++;
    writeIdx = (writeIdx + 1) % LOG_MAX_SAMPLES;

    if (recording && csvFile)
        writeCsvRow(mon);
}

void LogState::clear() {
    writeIdx = 0;
    sampleCount = 0;
    memset(data, 0, sizeof(data));
}

void LogState::startRecording() {
    // Generate default filename with timestamp
    char defaultName[128];
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    snprintf(defaultName, sizeof(defaultName), "ecu_log_%04d%02d%02d_%02d%02d%02d.csv",
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
        t->tm_hour, t->tm_min, t->tm_sec);

    // File save dialog
    char path[260] = {};
    strncpy(path, defaultName, sizeof(path) - 1);

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.lpstrDefExt = "csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameA(&ofn)) return;

    csvFile = fopen(path, "w");
    if (!csvFile) {
        snprintf(csvError, sizeof(csvError), "Could not create the selected CSV file");
        return;
    }

    strncpy(csvPath, path, sizeof(csvPath) - 1);
    csvPath[sizeof(csvPath) - 1] = '\0';
    csvError[0] = '\0';
    csvRows = 0;
    recordingElapsed = 0.0f;
    recording = true;
    writeCsvHeader();
}

void LogState::stopRecording() {
    if (csvFile) {
        if (fclose(csvFile) != 0 && !csvError[0])
            snprintf(csvError, sizeof(csvError), "CSV close failed; the final buffered rows may be incomplete");
        csvFile = nullptr;
    }
    recording = false;
}

void LogState::writeCsvHeader() {
    if (!csvFile) return;
    bool ok = fprintf(csvFile, "Time(s)") >= 0;
    for (int s = 0; s < SIG_COUNT; s++) {
        if (SIGNAL_DEFS[s].unit[0])
            ok = ok && fprintf(csvFile, ",%s(%s)", SIGNAL_DEFS[s].name, SIGNAL_DEFS[s].unit) >= 0;
        else
            ok = ok && fprintf(csvFile, ",%s", SIGNAL_DEFS[s].name) >= 0;
    }
    ok = ok && fprintf(csvFile, "\n") >= 0 && fflush(csvFile) == 0;
    if (!ok) {
        snprintf(csvError, sizeof(csvError), "CSV header write failed; recording stopped");
        stopRecording();
    }
}

void LogState::writeCsvRow(const MonitorData& mon) {
    if (!csvFile) return;
    const float timeSec = recordingElapsed;
    bool ok = fprintf(csvFile, "%.3f", timeSec) >= 0;
    for (int s = 0; s < SIG_COUNT; s++) {
        float v = getSignalValue((LogSignal)s, mon);
        ok = ok && fprintf(csvFile, ",%.3f", v) >= 0;
    }
    ok = ok && fprintf(csvFile, "\n") >= 0;
    csvRows++;

    // Flush periodically (every 100 rows)
    if ((csvRows % 100) == 0) ok = ok && fflush(csvFile) == 0;
    if (!ok || ferror(csvFile)) {
        snprintf(csvError, sizeof(csvError), "CSV write failed after %d rows; recording stopped", csvRows);
        stopRecording();
    }
}

// ============================================================
//  Auto-scale helper for graph ranges
// ============================================================
static void AutoRange(const float* buf, int count, int writeIdx, int maxSamples,
                      float minRange, float& lo, float& hi) {
    lo = 1e9f; hi = -1e9f;
    for (int i = 0; i < count; i++) {
        int idx = (writeIdx - count + i + maxSamples) % maxSamples;
        if (buf[idx] < lo) lo = buf[idx];
        if (buf[idx] > hi) hi = buf[idx];
    }
    if (count == 0) { lo = 0; hi = minRange; return; }
    float range = hi - lo;
    if (range < minRange) {
        float mid = (lo + hi) * 0.5f;
        lo = mid - minRange * 0.5f;
        hi = mid + minRange * 0.5f;
    }
    float pad = (hi - lo) * 0.05f;
    lo -= pad;
    hi += pad;
}

// ============================================================
//  Draw a single graph panel with one or more signals overlaid
// ============================================================
// Compute a nice step size for axis ticks
static float NiceStep(float range) {
    float raw = range / 5.0f;
    float mag = powf(10.0f, floorf(log10f(fabsf(raw) + 1e-9f)));
    float norm = raw / mag;
    float nice;
    if (norm <= 1.0f) nice = 1.0f;
    else if (norm <= 2.0f) nice = 2.0f;
    else if (norm <= 5.0f) nice = 5.0f;
    else nice = 10.0f;
    return nice * mag;
}

static void DrawLogGraph(LogState& log, const LogSignal* sigs, int nSigs,
                         float width, float height, const char* id) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                      IM_COL32(18, 18, 26, 255), S(4));
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                IM_COL32(45, 45, 65, 255), S(4));

    // Layout: one axis on the left (signal 0), rest stacked on the right.
    const float perAxisW     = S(44);
    const float legendH      = S(16);
    const float xAxisH       = S(18);
    const int   numRightAxes = nSigs > 1 ? nSigs - 1 : 0;
    const float leftAxisW    = nSigs > 0 ? perAxisW : 0.0f;
    const float rightAxisW   = (float)numRightAxes * perAxisW;

    float cX = pos.x + leftAxisW;
    float cY = pos.y + legendH;
    float cW = width  - leftAxisW - rightAxisW - S(4);
    float cH = height - legendH - xAxisH;

    if (cW < S(10) || cH < S(10)) { ImGui::Dummy(ImVec2(width, height)); return; }

    // Visible sample count
    int visibleSamples = (int)(log.timeWindowSec / log.sampleInterval);
    if (visibleSamples > log.sampleCount) visibleSamples = log.sampleCount;
    if (visibleSamples < 2) visibleSamples = 2;

    // Per-signal Y ranges
    float sigLo[SIG_COUNT], sigHi[SIG_COUNT];
    for (int si = 0; si < nSigs; si++) {
        LogSignal sig = sigs[si];
        const SignalDef& def = SIGNAL_DEFS[sig];
        if (log.autoScale) {
            float minRange = (def.defaultMax - def.defaultMin) * 0.1f;
            if (minRange < 1.0f) minRange = 1.0f;
            AutoRange(log.data[sig], visibleSamples, log.writeIdx, LOG_MAX_SAMPLES,
                      minRange, sigLo[si], sigHi[si]);
        } else {
            sigLo[si] = log.customMin[sig];
            sigHi[si] = log.customMax[sig];
            if (!std::isfinite(sigLo[si]) || !std::isfinite(sigHi[si]) || sigHi[si] <= sigLo[si]) {
                sigLo[si] = def.defaultMin;
                sigHi[si] = def.defaultMax;
            }
        }
    }

    // Plot area background
    dl->AddRectFilled(ImVec2(cX, cY), ImVec2(cX + cW, cY + cH),
                      IM_COL32(10, 10, 18, 255));

    // --- Left y-axis: signal 0, in its color ---
    if (nSigs > 0) {
        const SignalDef& def0 = SIGNAL_DEFS[sigs[0]];
        float lo = sigLo[0], hi = sigHi[0];
        float yRange = hi - lo;
        if (yRange < 1e-6f) yRange = 1.0f;
        float yStep = NiceStep(yRange);
        if (yStep < 1e-9f) yStep = 1.0f;
        float yTickStart = ceilf(lo / yStep) * yStep;

        dl->AddLine(ImVec2(cX, cY), ImVec2(cX, cY + cH), def0.color, S(1.5f));

        for (float yv = yTickStart; yv <= hi + yStep * 0.01f; yv += yStep) {
            float f = (yv - lo) / yRange;
            if (f < -0.01f || f > 1.01f) continue;
            float y = cY + cH * (1.0f - f);

            dl->AddLine(ImVec2(cX, y), ImVec2(cX + cW, y), IM_COL32(35, 35, 50, 100)); // grid
            dl->AddLine(ImVec2(cX - S(4), y), ImVec2(cX, y), def0.color, 1.0f);         // tick

            char lb[16];
            if (fabsf(yv) >= 100.0f || yStep >= 1.0f) snprintf(lb, sizeof(lb), "%.0f", yv);
            else snprintf(lb, sizeof(lb), "%.1f", yv);
            ImVec2 ts = ImGui::CalcTextSize(lb);
            float lx = cX - ts.x - S(5);
            if (lx < pos.x + S(2)) lx = pos.x + S(2);
            dl->AddText(ImVec2(lx, y - ts.y * 0.5f), def0.color, lb);
        }
    }

    // --- Right y-axes: signals 1..nSigs-1, each in its own color ---
    for (int ri = 0; ri < numRightAxes; ri++) {
        int si = ri + 1;
        const SignalDef& defi = SIGNAL_DEFS[sigs[si]];
        float lo = sigLo[si], hi = sigHi[si];
        float yRange = hi - lo;
        if (yRange < 1e-6f) yRange = 1.0f;
        float yStep = NiceStep(yRange);
        if (yStep < 1e-9f) yStep = 1.0f;
        float yTickStart = ceilf(lo / yStep) * yStep;
        float axLineX = cX + cW + (float)ri * perAxisW;

        dl->AddLine(ImVec2(axLineX, cY), ImVec2(axLineX, cY + cH), defi.color, S(1.5f));

        for (float yv = yTickStart; yv <= hi + yStep * 0.01f; yv += yStep) {
            float f = (yv - lo) / yRange;
            if (f < -0.01f || f > 1.01f) continue;
            float y = cY + cH * (1.0f - f);

            dl->AddLine(ImVec2(axLineX, y), ImVec2(axLineX + S(4), y), defi.color, 1.0f); // tick

            char lb[16];
            if (fabsf(yv) >= 100.0f || yStep >= 1.0f) snprintf(lb, sizeof(lb), "%.0f", yv);
            else snprintf(lb, sizeof(lb), "%.1f", yv);
            ImVec2 ts = ImGui::CalcTextSize(lb);
            float lx = axLineX + S(6);
            float zoneRight = axLineX + perAxisW - S(2);
            if (lx + ts.x > zoneRight) lx = zoneRight - ts.x;
            dl->AddText(ImVec2(lx, y - ts.y * 0.5f), defi.color, lb);
        }
    }

    // --- X-axis (time) ticks and grid ---
    float timeStep = NiceStep(log.timeWindowSec);
    if (timeStep < 1.0f) timeStep = 1.0f;
    for (float t = timeStep; t < log.timeWindowSec; t += timeStep) {
        float f = 1.0f - t / log.timeWindowSec;
        float x = cX + cW * f;

        dl->AddLine(ImVec2(x, cY), ImVec2(x, cY + cH), IM_COL32(35, 35, 50, 100));
        dl->AddLine(ImVec2(x, cY + cH), ImVec2(x, cY + cH + S(3)), IM_COL32(70, 70, 90, 200));

        char tb[16];
        snprintf(tb, sizeof(tb), "-%.0fs", t);
        ImVec2 ts = ImGui::CalcTextSize(tb);
        dl->AddText(ImVec2(x - ts.x * 0.5f, cY + cH + S(4)), IM_COL32(100, 100, 130, 220), tb);
    }
    {
        ImVec2 ts = ImGui::CalcTextSize("now");
        dl->AddText(ImVec2(cX + cW - ts.x, cY + cH + S(4)), IM_COL32(100, 100, 130, 220), "now");
    }

    // Axis border lines
    dl->AddLine(ImVec2(cX, cY), ImVec2(cX, cY + cH), IM_COL32(60, 60, 80, 200));
    dl->AddLine(ImVec2(cX, cY + cH), ImVec2(cX + cW, cY + cH), IM_COL32(60, 60, 80, 200));

    // --- Signal polylines (clipped to plot area) ---
    dl->PushClipRect(ImVec2(cX, cY), ImVec2(cX + cW, cY + cH), true);

    static ImVec2 s_pts[LOG_MAX_SAMPLES];

    for (int si = 0; si < nSigs; si++) {
        LogSignal sig = sigs[si];
        const SignalDef& def = SIGNAL_DEFS[sig];
        float lo = sigLo[si], hi = sigHi[si];
        float range = hi - lo;
        if (range < 1e-6f) range = 1.0f;
        float invRange = 1.0f / range;
        float xScale   = cW / (float)(visibleSamples - 1);
        int   startIdx = (log.writeIdx - visibleSamples + LOG_MAX_SAMPLES) % LOG_MAX_SAMPLES;

        for (int i = 0; i < visibleSamples; i++) {
            int idx = (startIdx + i) % LOG_MAX_SAMPLES;
            float f = std::clamp((log.data[sig][idx] - lo) * invRange, 0.0f, 1.0f);
            s_pts[i] = ImVec2(cX + i * xScale, cY + cH * (1.0f - f));
        }
        dl->AddPolyline(s_pts, visibleSamples, def.color, ImDrawFlags_None, S(1.5f));
    }

    dl->PopClipRect();

    // --- Signal name labels above each axis (clipped to axis zone) ---
    if (nSigs > 0) {
        const SignalDef& def0 = SIGNAL_DEFS[sigs[0]];
        dl->PushClipRect(ImVec2(pos.x, pos.y), ImVec2(cX, cY), true);
        dl->AddText(ImVec2(pos.x + S(2), pos.y + S(2)), def0.color, def0.name);
        dl->PopClipRect();
    }
    for (int ri = 0; ri < numRightAxes; ri++) {
        int si = ri + 1;
        const SignalDef& defi = SIGNAL_DEFS[sigs[si]];
        float axLeft  = cX + cW + (float)ri * perAxisW;
        float axRight = axLeft + perAxisW;
        dl->PushClipRect(ImVec2(axLeft, pos.y), ImVec2(axRight, cY), true);
        dl->AddText(ImVec2(axLeft + S(2), pos.y + S(2)), defi.color, defi.name);
        dl->PopClipRect();
    }

    ImGui::Dummy(ImVec2(width, height));
}

// ============================================================
//  Main Logging Tab UI
// ============================================================
void DrawLoggingTab(LogState& log, const MonitorData& mon, bool connected,
                    bool monitoring, float dataAgeSeconds) {
    const bool fresh = connected && monitoring && mon.valid && dataAgeSeconds <= 1.0f;
    // Persistent connection/recording/write-health status bar.
    ImGui::TextColored(fresh ? ImVec4(0.30f, 0.72f, 0.54f, 1.0f)
                                 : ImVec4(0.88f, 0.32f, 0.28f, 1.0f),
                       !connected ? "DISCONNECTED" : !monitoring ? "CONNECTED / MONITORING OFF" :
                       fresh ? "CONNECTED / LIVE" : "CONNECTED / STALE");
    ImGui::SameLine(0, S(20));
    const char* destination = strrchr(log.csvPath, '\\');
    destination = destination ? destination + 1 : (log.csvPath[0] ? log.csvPath : "-");
    if (log.recording)
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.28f, 1.0f),
                           "RECORDING  %.1f s  |  %d rows  |  %s  |  write: %s",
                           log.recordingElapsed, log.csvRows, destination,
                           log.csvError[0] ? "FAILED" : "healthy");
    else
        ImGui::TextDisabled("Recording stopped  |  destination: %s", destination);
    if (log.csvError[0]) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.34f, 0.08f, 0.06f, 1.0f));
        if (ImGui::BeginChild("##csv_error_banner", ImVec2(0, ImGui::GetFrameHeight() + S(8)),
                              ImGuiChildFlags_Borders))
            ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.76f, 1.0f), "WRITE FAILURE: %s", log.csvError);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    if (!fresh) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            monitoring ? "No fresh monitor data" : "Monitoring is off");
        if (log.recording && ImGui::Button("Stop Recording")) log.stopRecording();
        if (log.csvError[0]) {
            ImGui::SameLine();
            if (ImGui::Button("Acknowledge write failure")) log.csvError[0] = '\0';
        }
        return;
    }

    float avail = ImGui::GetContentRegionAvail().x;

    // === Controls Bar ===
    // Recording controls
    if (log.recording) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Stop Recording")) log.stopRecording();
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC %d rows", log.csvRows);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.65f, 0.2f, 1.0f));
        if (ImGui::Button("Record to CSV")) log.startRecording();
        ImGui::PopStyleColor(2);
    }
    if (log.csvError[0]) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", log.csvError);
    }

    ImGui::SameLine(0, S(20));
    if (ImGui::Button("Clear Data")) log.clear();

    ImGui::SameLine(0, S(30));

    // Time window
    ImGui::SetNextItemWidth(S(120));
    ImGui::SliderFloat("Time Window", &log.timeWindowSec, 5.0f, 300.0f, "%.0f s");
    ImGui::SameLine(0, S(15));

    // Sample rate
    ImGui::SetNextItemWidth(S(100));
    int rateHz = (int)(1.0f / log.sampleInterval);
    if (log.recording) ImGui::BeginDisabled();
    if (ImGui::SliderInt("Sample Rate", &rateHz, 5, 50, "%d Hz")) {
        if (rateHz < 5) rateHz = 5;
        log.sampleInterval = 1.0f / rateHz;
    }
    if (log.recording) ImGui::EndDisabled();

    ImGui::SameLine(0, S(15));
    ImGui::Checkbox("Auto Scale", &log.autoScale);
    ImGui::SameLine(0, S(15));
    ImGui::Checkbox("Stacked", &log.stackGraphs);

    if (log.stackGraphs) {
        ImGui::SameLine(0, S(15));
        ImGui::Checkbox("Group compatible units", &log.groupByUnit);
        ImGui::SameLine(0, S(15));
        ImGui::SetNextItemWidth(S(100));
        ImGui::SliderFloat("Height", &log.graphHeight, 100.0f, 400.0f, "%.0f px");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === Two-panel layout: signal selector on left, graphs on right ===
    float sidebarW = S(210);
    float graphAreaW = avail - sidebarW - S(10);

    // --- Signal Selector Panel ---
    ImGui::BeginChild("##logsignals", ImVec2(sidebarW, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Signals");
    ImGui::Separator();

    // Quick presets
    if (ImGui::SmallButton("Engine")) {
        memset(log.enabled, 0, sizeof(log.enabled));
        log.enabled[SIG_RPM] = log.enabled[SIG_MAP] = log.enabled[SIG_TPS] = log.enabled[SIG_AFR] = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fuel")) {
        memset(log.enabled, 0, sizeof(log.enabled));
        log.enabled[SIG_RPM] = log.enabled[SIG_INJ_PW] = log.enabled[SIG_VE] = log.enabled[SIG_AFR] = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("All")) {
        for (int s = 0; s < SIG_COUNT; s++) log.enabled[s] = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        memset(log.enabled, 0, sizeof(log.enabled));
    }

    ImGui::Spacing();

    // Individual signal checkboxes with color indicators, grouped
    for (int s = 0; s < SIG_COUNT; s++) {
        // Group separators
        if (s == SIG_VE)  { ImGui::Separator(); ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.5f, 1.0f), "Engine"); }
        if (s == SIG_CLT) { ImGui::Separator(); ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.5f, 1.0f), "Temps / Power"); }
        if (s == SIG_TARGET_IDLE) { ImGui::Separator(); ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.5f, 1.0f), "Idle / Aux"); }
        if (s == SIG_SPEED) { ImGui::Separator(); ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.5f, 1.0f), "Vehicle"); }
        if (s == SIG_SYNC) { ImGui::Separator(); ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.5f, 1.0f), "Accel / Corrections"); }

        const SignalDef& def = SIGNAL_DEFS[s];

        // Color swatch
        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float swSz = S(10);
        dl->AddRectFilled(ImVec2(cp.x, cp.y + S(3)),
                          ImVec2(cp.x + swSz, cp.y + S(13)),
                          log.enabled[s] ? def.color : IM_COL32(50, 50, 60, 255));
        ImGui::Dummy(ImVec2(swSz + S(4), 0));
        ImGui::SameLine(0, 0);

        char label[48];
        if (def.unit[0])
            snprintf(label, sizeof(label), "%s (%s)", def.name, def.unit);
        else
            snprintf(label, sizeof(label), "%s", def.name);
        ImGui::Checkbox(label, &log.enabled[s]);

        // Show current value when enabled
        if (log.enabled[s] && mon.valid) {
            ImGui::SameLine();
            float v = LogState::getSignalValue((LogSignal)s, mon);
            char vb[16];
            if (v == (int)v) snprintf(vb, sizeof(vb), "%.0f", v);
            else snprintf(vb, sizeof(vb), "%.1f", v);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "%s", vb);
        }
    }

    // Custom range editors (when not auto-scaling)
    bool hasEnabledSignal = false;
    for (int s = 0; s < SIG_COUNT; ++s) if (log.enabled[s]) { hasEnabledSignal = true; break; }
    if (!log.autoScale && hasEnabledSignal) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Ranges");
        for (int s = 0; s < SIG_COUNT; s++) {
            if (!log.enabled[s]) continue;
            ImGui::PushID(s);
            ImGui::Text("%s:", SIGNAL_DEFS[s].name);
            ImGui::SetNextItemWidth(S(60));
            ImGui::DragFloat("##min", &log.customMin[s], 1.0f);
            const bool minCommitted = ImGui::IsItemDeactivatedAfterEdit();
            const bool invalidRange = !std::isfinite(log.customMin[s]) || !std::isfinite(log.customMax[s]) ||
                                      log.customMin[s] >= log.customMax[s];
            DrawInvalidInputOutline(invalidRange);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(S(60));
            ImGui::DragFloat("##max", &log.customMax[s], 1.0f);
            DrawInvalidInputOutline(invalidRange);
            const bool maxCommitted = ImGui::IsItemDeactivatedAfterEdit();
            if (minCommitted || maxCommitted) {
                if (!std::isfinite(log.customMin[s]) || !std::isfinite(log.customMax[s])) {
                    log.customMin[s] = SIGNAL_DEFS[s].defaultMin;
                    log.customMax[s] = SIGNAL_DEFS[s].defaultMax;
                } else if (log.customMin[s] >= log.customMax[s]) {
                    if (minCommitted) log.customMin[s] = log.customMax[s] - 0.001f;
                    else log.customMax[s] = log.customMin[s] + 0.001f;
                }
            }
            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // --- Graph Area ---
    ImGui::BeginChild("##loggraphs", ImVec2(graphAreaW, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    const float plotWidth = ScrollSafeWidth(ImGui::GetContentRegionAvail().x);

    // Collect enabled signals
    LogSignal activeSigs[SIG_COUNT];
    int nActive = 0;
    for (int s = 0; s < SIG_COUNT; s++) {
        if (log.enabled[s])
            activeSigs[nActive++] = (LogSignal)s;
    }

    if (nActive == 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
            "Select signals from the left panel to display graphs");
    } else if (log.stackGraphs) {
        // Stacked: one graph per signal, or pairs with compatible units.
        float gh = S(log.graphHeight);
        bool drawn[SIG_COUNT] = {};
        for (int i = 0; i < nActive; i++) {
            if (drawn[i]) continue;
            LogSignal panelSignals[2] = { activeSigs[i], activeSigs[i] };
            int panelCount = 1;
            drawn[i] = true;
            if (log.groupByUnit) {
                for (int j = i + 1; j < nActive; ++j) {
                    if (SIGNAL_DEFS[activeSigs[i]].unit[0] && !drawn[j] && strcmp(SIGNAL_DEFS[activeSigs[i]].unit,
                                            SIGNAL_DEFS[activeSigs[j]].unit) == 0) {
                        panelSignals[panelCount++] = activeSigs[j];
                        drawn[j] = true;
                        break; // never expose more than two Y scales on one panel
                    }
                }
            }
            char gid[32];
            snprintf(gid, sizeof(gid), "##graph_%d", activeSigs[i]);
            DrawLogGraph(log, panelSignals, panelCount, plotWidth, gh, gid);
            if (i < nActive - 1) ImGui::Spacing();
        }
    } else {
        // Overlaid: all signals in one graph
        float gh = ImGui::GetContentRegionAvail().y - S(10);
        if (gh < S(100)) gh = S(100);
        DrawLogGraph(log, activeSigs, nActive, plotWidth, gh, "##graphall");
    }

    ImGui::EndChild();
}
