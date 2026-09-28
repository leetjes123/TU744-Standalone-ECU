#include "storage.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <cerrno>
#include <cmath>
#include <sstream>
#include <iomanip>

bool GetWizardDataDirectory(std::string& pathOut) {
    char localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    pathOut = std::string(localAppData) + "\\TuningWizard";
    if (!CreateDirectoryA(pathOut.c_str(), nullptr)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
        const DWORD attrs = GetFileAttributesA(pathOut.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;
    }
    return true;
}

bool CreateCalibrationBackup(const CalBuffer& cal, const char* reason, std::string* pathOut) {
    if (!cal.loaded) return true;

    std::string dataDir;
    if (!GetWizardDataDirectory(dataDir)) return false;
    char backupDir[MAX_PATH] = {};
    snprintf(backupDir, sizeof(backupDir), "%s\\backups", dataDir.c_str());
    if (!CreateDirectoryA(backupDir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char finalPath[MAX_PATH] = {};
    snprintf(finalPath, sizeof(finalPath), "%s\\%04u%02u%02u-%02u%02u%02u-%03u-%s.twbackup",
        backupDir, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds,
        reason ? reason : "snapshot");
    char tempPath[MAX_PATH] = {};
    if (!GetTempFileNameA(backupDir, "tw", 0, tempPath)) return false;

    FILE* file = fopen(tempPath, "wb");
    if (!file) { DeleteFileA(tempPath); return false; }
    const bool wrote = fwrite(cal.data, 1, TOTAL_SIZE, file) == TOTAL_SIZE;
    const bool flushed = wrote && fflush(file) == 0 && _commit(_fileno(file)) == 0;
    const bool closed = fclose(file) == 0;
    const bool ok = wrote && flushed && closed;
    if (!ok) { DeleteFileA(tempPath); return false; }
    if (!MoveFileExA(tempPath, finalPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tempPath);
        return false;
    }
    if (pathOut) *pathOut = finalPath;
    return true;
}

namespace {
std::string PresetPath() {
    std::string dir;
    return GetWizardDataDirectory(dir) ? dir + "\\ui_presets.txt" : std::string();
}
}

bool LoadUiPresets(UiPresetData* presets, int count) {
    if (!presets || count <= 0 || count > 16) return false;
    const std::string path = PresetPath();
    if (path.empty()) return false;
    FILE* file = fopen(path.c_str(), "r");
    if (!file) return errno == ENOENT;
    char header[32] = {};
    if (!fgets(header, sizeof(header), file) || strcmp(header, "TW_UI_PRESETS_V1\n") != 0) {
        fclose(file); return false;
    }
    UiPresetData parsed[16] = {};
    for (int i = 0; i < count; ++i) {
        char line[256] = {};
        if (!fgets(line, sizeof(line), file) || !strchr(line, '\n')) { fclose(file); return false; }
        long long valid = 0, workspace = 0, layout = 0, theme = 0, grouping = 0;
        unsigned long long mask = 0;
        double graphHeight = 0.0, timeWindow = 0.0;
        std::istringstream stream(line);
        if (!(stream >> valid >> workspace >> layout >> theme >> std::hex >> mask >> std::dec >> grouping
                     >> graphHeight >> timeWindow)) { fclose(file); return false; }
        std::string trailing;
        if (stream >> trailing) { fclose(file); return false; }
        UiPresetData& p = parsed[i];
        if (valid < 0 || valid > 1 || workspace < 0 || workspace > 9 || layout < 0 || layout > 2 ||
            theme < 0 || theme > 2 || mask > 0xFFFFFFFFull || grouping < 0 || grouping > 1 ||
            !std::isfinite(graphHeight) || graphHeight < 80.0 || graphHeight > 800.0 ||
            !std::isfinite(timeWindow) || timeWindow < 1.0 || timeWindow > 600.0) { fclose(file); return false; }
        p.valid = valid != 0; p.workspace = (int)workspace; p.dashboardLayout = (int)layout;
        p.theme = (int)theme; p.signalMask = (uint32_t)mask; p.groupByUnit = grouping != 0;
        p.graphHeight = (float)graphHeight; p.timeWindowSec = (float)timeWindow;
    }
    fclose(file);
    for (int i = 0; i < count; ++i) presets[i] = parsed[i];
    return true;
}

bool SaveUiPresets(const UiPresetData* presets, int count) {
    if (!presets || count <= 0 || count > 16) return false;
    std::string dir;
    if (!GetWizardDataDirectory(dir)) return false;
    char tempPath[MAX_PATH] = {};
    if (!GetTempFileNameA(dir.c_str(), "lrp", 0, tempPath)) return false;
    FILE* file = fopen(tempPath, "w");
    if (!file) { DeleteFileA(tempPath); return false; }
    bool ok = fputs("TW_UI_PRESETS_V1\n", file) >= 0;
    for (int i = 0; ok && i < count; ++i) {
        const UiPresetData& p = presets[i];
        if (p.workspace < 0 || p.workspace > 9 || p.dashboardLayout < 0 || p.dashboardLayout > 2 ||
            p.theme < 0 || p.theme > 2 || !std::isfinite(p.graphHeight) || p.graphHeight < 80.0f ||
            p.graphHeight > 800.0f || !std::isfinite(p.timeWindowSec) || p.timeWindowSec < 1.0f ||
            p.timeWindowSec > 600.0f) { ok = false; break; }
        ok = fprintf(file, "%d %d %d %d %08X %d %.3f %.3f\n", p.valid ? 1 : 0,
                     p.workspace, p.dashboardLayout, p.theme, p.signalMask,
                     p.groupByUnit ? 1 : 0, p.graphHeight, p.timeWindowSec) > 0;
    }
    const bool flushed = ok && fflush(file) == 0 && _commit(_fileno(file)) == 0;
    const bool closed = fclose(file) == 0;
    if (!ok || !flushed || !closed) { DeleteFileA(tempPath); return false; }
    const std::string finalPath = PresetPath();
    if (!MoveFileExA(tempPath, finalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tempPath); return false;
    }
    return true;
}
