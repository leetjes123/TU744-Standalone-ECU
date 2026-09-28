#pragma once
#include "calibration.h"
#include <string>
#include <cstdint>

struct UiPresetData {
    bool valid = false;
    int workspace = 0;
    int dashboardLayout = 1;
    int theme = 0;
    uint32_t signalMask = 0;
    bool groupByUnit = true;
    float graphHeight = 200.0f;
    float timeWindowSec = 30.0f;
};

bool CreateCalibrationBackup(const CalBuffer& cal, const char* reason, std::string* pathOut = nullptr);
bool GetWizardDataDirectory(std::string& pathOut);
bool LoadUiPresets(UiPresetData* presets, int count);
bool SaveUiPresets(const UiPresetData* presets, int count);
