#pragma once
#include "calibration.h"
#include <string>
#include <vector>

enum class IssueSeverity { Warning, Error };
enum class IssueTarget { None, Scalar, Table, Axis, Dropdown };

struct CalibrationIssue {
    IssueSeverity severity;
    std::string message;
    std::string id;
    IssueTarget target = IssueTarget::None;
    int targetIndex = -1;
    int row = -1;
    int col = -1;
    float currentValue = 0.0f;
    std::string allowed;
    std::string suggestion;
};

struct CalibrationRange {
    int start;
    int end;
};

std::vector<CalibrationIssue> ValidateCalibration(const CalBuffer& cal);
std::vector<CalibrationRange> BuildDirtyRanges(const unsigned char* data,
                                               const unsigned char* ecuData,
                                               bool ecuSynced);
int CountRangeBytes(const std::vector<CalibrationRange>& ranges);
const char* FeatureForOffset(int offset);

extern const CalibrationRange STRUCTURAL_RANGES[];
extern const int NUM_STRUCTURAL_RANGES;
bool IsStructuralOffset(int offset);
std::vector<CalibrationRange> SplitLiveWrites(const std::vector<CalibrationRange>& ranges);
