#pragma once
#include <string>

struct CalibrationPage {
    const char* id;
    const char* section;
    const char* group;
    const char* label; // Empty: show tables directly inside the group.
    const char* settingsLabel;
};
extern const CalibrationPage CALIBRATION_PAGES[];
extern const int NUM_CALIBRATION_PAGES;
const CalibrationPage* CalibrationPageFor(const char* category,const char* name,int offset);
const CalibrationPage* FindCalibrationPage(const char* id);
bool CalibrationPageMatches(const char* filter,const char* category,const char* name,int offset);
bool CalibrationSearchMatches(const char* query,const char* category,const char* name,int offset);
const char* CalibrationTableLabel(int offset,const char* original);
