#pragma once
#include "imgui.h"
#include <algorithm>

inline void DrawInvalidInputOutline(bool invalid) {
    if (!invalid) return;
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - 1.0f, min.y - 1.0f),
        ImVec2(max.x + 1.0f, max.y + 1.0f), IM_COL32(235, 55, 55, 255),
        ImGui::GetStyle().FrameRounding, 0, 2.0f);
}

inline float ScrollSafeWidth(float width) {
    // GetContentRegionAvail() already excludes a child window's scrollbar.
    return std::max(1.0f, width - ImGui::GetStyle().ItemSpacing.x);
}
