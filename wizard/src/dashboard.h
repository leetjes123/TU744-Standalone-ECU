#pragma once
#include "protocol.h"
#include "imgui.h"

// Draw the full live dashboard panel (in Dashboard tab)
enum class DashboardLayout { Compact = 0, Standard = 1, Diagnostic = 2 };

void DrawDashboard(const MonitorData& mon, bool connected, bool monitoring, float dataAgeSeconds,
                   DashboardLayout& layout, bool narrowband);

// Draw an inline dashboard panel below the tabs area (always visible).
// Shows key gauges with dashes when not connected.
void DrawInlineDashboard(const MonitorData& mon, bool connected, bool monitoring,
                         float dataAgeSeconds, bool narrowband);
