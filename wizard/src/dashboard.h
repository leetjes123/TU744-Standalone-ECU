#pragma once
#include "protocol.h"
#include "imgui.h"
#include <string>

// Draw the full live dashboard panel (in Dashboard tab)
enum class DashboardLayout { Compact = 0, Standard = 1, Diagnostic = 2 };

void DrawDashboard(const MonitorData& mon, bool connected, bool monitoring, float dataAgeSeconds,
                   DashboardLayout& layout, bool narrowband);

// Format the persistent monitor row so its wrapped height can be reserved.
// Unavailable readings remain visible as labeled dashes.
std::string InlineDashboardText(const MonitorData& mon, bool fresh, float stoichAfr);
