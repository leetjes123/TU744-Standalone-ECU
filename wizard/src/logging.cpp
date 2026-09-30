#include "logging.h"
#include "app.h"
#include "ui_helpers.h"
#include "graph_math.h"
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
    case SIG_KNOCK_MV: return mon.knockAvailable && mon.knockMv!=65535 ? float(mon.knockMv) : NAN;
    case SIG_KNOCK_DETECTED: return mon.knockAvailable ? float((mon.knockFlags&1)!=0) : NAN;
    case SIG_KNOCK_RETARD: return mon.knockAvailable ? mon.knockRetard : NAN;
    case SIG_KNOCK_VALID: return mon.knockAvailable ? float((mon.knockFlags&2)!=0) : NAN;
    case SIG_KNOCK_FAULT: return mon.knockAvailable ? float((mon.knockFlags&16)!=0) : NAN;
    default: return 0;
    }
}
// ============================================================
//  LogState methods
// ============================================================
void LogState::init() {
    // Enable a sensible default set
    enabled[SIG_RPM]    = true;
    enabled[SIG_MAP]    = true;
    enabled[SIG_TPS]    = true;
    enabled[SIG_AFR]    = true;

    enabled[SIG_TARGET_AFR] = true;
    enabled[SIG_STFT] = true;
}

void LogState::pushSample(const MonitorData& mon,double timeSeconds) {
    if(timeSeconds<0 || !std::isfinite(timeSeconds)) timeSeconds=GetTickCount64()*.001;
    if(sampleCount) timeSeconds=std::max(timeSeconds,sampleTimes[(writeIdx+LOG_MAX_SAMPLES-1)%LOG_MAX_SAMPLES]+.000001);
    sampleTimes[writeIdx]=timeSeconds;
    for (int s = 0; s < SIG_COUNT; s++)
        data[s][writeIdx] = getSignalValue((LogSignal)s, mon);

    if (sampleCount < LOG_MAX_SAMPLES) sampleCount++;
    writeIdx = (writeIdx + 1) % LOG_MAX_SAMPLES;

    if(recording && csvFile) {
        if(recordingBaseTime<0) recordingBaseTime=timeSeconds;
        recordingElapsed=float(timeSeconds-recordingBaseTime);
        writeCsvRow(mon);
    }
}

void LogState::clear() {
    writeIdx = 0;
    sampleCount = 0;
    viewer.log={}; viewer.parseOk=false; viewer.followLatest=true;
    viewer.hasSelection=false; viewer.selecting=false; viewer.cursorLocked=false;
    viewerWriteIdx=viewerSampleCount=-1;
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
    recordingBaseTime = -1;
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

void LogState::refreshViewer() {
    auto& v=viewer;
    v.liveMode=true;
    if(v.log.columnNames.empty()) {
        v.log.columnNames.push_back("Time(s)");
        for(int i=0;i<SIG_COUNT;++i)
            v.log.columnNames.push_back(MonitorColumnName(i));
    }
    if(v.signals.empty()) {
        for(int i=0;i<SIG_COUNT;++i) {
            const auto& def=SIGNAL_DEFS[i];
            LVSignal signal;
            signal.colIndex=i+1; signal.visible=enabled[i]; signal.color=def.color;
            signal.fullMin=signal.yMin=def.defaultMin; signal.fullMax=signal.yMax=def.defaultMax;
            signal.plotIndex=i==SIG_RPM?0:(i==SIG_MAP || i==SIG_TPS)?1:2;
            v.signals.push_back(signal);
        }
        v.signals[SIG_TARGET_AFR].visible=true;
        v.signals[SIG_STFT].visible=true;
        for(const char* label:{"Engine speed","Load / throttle","Mixture / corrections"}) {
            LVPlot plot; snprintf(plot.label,sizeof(plot.label),"%s",label); v.plots.push_back(plot);
        }
        v.liveWindow=timeWindowSec;
    }
    // Pausing freezes only the view. Accepted packets and CSV recording continue.
    if(!v.followLatest && v.parseOk) return;
    if(sampleCount<2 || (viewerWriteIdx==writeIdx && viewerSampleCount==sampleCount)) return;
    const int oldest=(writeIdx-sampleCount+LOG_MAX_SAMPLES)%LOG_MAX_SAMPLES;
    const double origin=sampleTimes[oldest];
    const float shift=float(origin-v.timeOrigin);
    if(v.parseOk) {
        v.cursorTime=std::max(0.0f,v.cursorTime-shift); v.viewStart-=shift; v.viewEnd-=shift;
        v.selectionStart-=shift; v.selectionEnd-=shift;
        if(std::max(v.selectionStart,v.selectionEnd)<0) v.hasSelection=false;
    }
    v.timeOrigin=origin;
    v.log.time.resize(sampleCount); v.log.columns.resize(SIG_COUNT+1);
    for(auto& column:v.log.columns) column.resize(sampleCount);
    for(int i=0;i<sampleCount;++i) {
        const int index=(oldest+i)%LOG_MAX_SAMPLES;
        const float time=float(sampleTimes[index]-origin);
        v.log.time[i]=v.log.columns[0][i]=time;
        for(int s=0;s<SIG_COUNT;++s) v.log.columns[s+1][i]=data[s][index];
    }
    for(auto& signal:v.signals) {
        const auto stats=SelectedGraphStatistics(v.log.time,v.log.columns[signal.colIndex],v.log.columnNames[signal.colIndex],0,v.log.time.back());
        if(stats.count) { signal.fullMin=stats.minimum; signal.fullMax=stats.maximum; }
    }
    v.log.sampleCount=sampleCount; v.log.duration=v.log.time.back();
    v.parseOk=v.log.duration>0;
    if(v.followLatest) {
        v.viewEnd=v.log.duration; v.viewStart=std::max(0.0f,v.viewEnd-v.liveWindow);
        if(!v.cursorLocked) v.cursorTime=v.viewEnd;
    }
    viewerWriteIdx=writeIdx; viewerSampleCount=sampleCount;
}

void DrawLoggingTab(LogState& log,const MonitorData& mon,bool connected,bool monitoring,float age) {
    const bool fresh=connected && monitoring && mon.valid && age<1;
    ImGui::TextColored(ImGui::GetStyleColorVec4(fresh?ImGuiCol_PlotLines:ImGuiCol_PlotHistogram),"%s",
        !connected?"DISCONNECTED":!monitoring?"MONITORING OFF":fresh?"LIVE DATA":"DATA STALE / PAUSED");
    if(log.recording) {
        ImGui::SameLine(); ImGui::Text("Recording %.1f s / %d rows",log.recordingElapsed,log.csvRows);
        if(ImGui::Button("Stop recording")) log.stopRecording();
    } else {
        ImGui::BeginDisabled(!fresh);
        if(ImGui::Button("Record to CSV")) log.startRecording();
        ImGui::EndDisabled();
    }
    ImGui::SameLine(); if(ImGui::Button("Clear history")) log.clear();
    ImGui::SameLine(); ImGui::TextDisabled("Capture follows accepted ECU packets");
    if(log.csvPath[0]) ImGui::TextWrapped("CSV: %s",log.csvPath);
    if(log.csvError[0]) {
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram),"CSV write failure: %s",log.csvError);
        if(ImGui::SmallButton("Acknowledge write failure")) log.csvError[0]=0;
    }
    if(!fresh) ImGui::TextDisabled("Showing retained history. No fresh readings are being displayed.");
    log.refreshViewer();
    DrawLogViewer(log.viewer);
    for(int i=0;i<int(log.viewer.signals.size()) && i<SIG_COUNT;++i) log.enabled[i]=log.viewer.signals[i].visible;
    log.timeWindowSec=log.viewer.liveWindow;
}
