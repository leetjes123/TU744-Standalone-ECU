#pragma once
#include "calibration.h"
#include "undo.h"
#include "protocol.h"

// Get tooltip text for a setting (scalar, flag, dropdown, or table) by name.
// Returns nullptr if no tooltip is defined.
const char* GetTooltip(const char* name);

// Get a help blurb for a calibration category (e.g. "Fuel", "Idle").
// Returns nullptr if no help is defined.
const char* GetCategoryHelp(const char* category);
