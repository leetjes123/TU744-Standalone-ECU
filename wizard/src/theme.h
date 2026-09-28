#pragma once

enum class UiTheme { Workshop = 0, HighContrast = 1, ColorVisionSafe = 2 };

void ApplyUiTheme(UiTheme theme, float dpiScale = 1.0f);
const char* UiThemeName(UiTheme theme);
UiTheme CurrentUiTheme();
