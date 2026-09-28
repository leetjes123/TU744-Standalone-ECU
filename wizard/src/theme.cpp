#include "theme.h"
#include "imgui.h"

namespace { UiTheme currentTheme = UiTheme::Workshop; }

UiTheme CurrentUiTheme() { return currentTheme; }

const char* UiThemeName(UiTheme theme) {
    switch (theme) {
        case UiTheme::HighContrast: return "High contrast";
        case UiTheme::ColorVisionSafe: return "Color-vision safe";
        default: return "Workshop";
    }
}

void ApplyUiTheme(UiTheme theme, float dpiScale) {
    currentTheme = theme;
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);
    ImVec4* c = style.Colors;

    ImVec4 accent(0.95f, 0.85f, 0.15f, 1.0f);
    ImVec4 selected(0.10f, 0.20f, 0.40f, 1.0f);
    if (theme == UiTheme::HighContrast) {
        c[ImGuiCol_Text] = ImVec4(1, 1, 1, 1);
        c[ImGuiCol_TextDisabled] = ImVec4(0.72f, 0.72f, 0.72f, 1);
        c[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.025f, 0.025f, 1);
        c[ImGuiCol_ChildBg] = ImVec4(0.045f, 0.045f, 0.045f, 1);
        c[ImGuiCol_PopupBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.99f);
        c[ImGuiCol_Border] = ImVec4(0.88f, 0.88f, 0.88f, 1);
        accent = ImVec4(1.0f, 0.82f, 0.0f, 1);
        selected = ImVec4(0.0f, 0.42f, 0.78f, 1);
    } else if (theme == UiTheme::ColorVisionSafe) {
        c[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.94f, 1);
        c[ImGuiCol_TextDisabled] = ImVec4(0.64f, 0.65f, 0.64f, 1);
        c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.085f, 0.095f, 1);
        c[ImGuiCol_ChildBg] = ImVec4(0.095f, 0.105f, 0.115f, 1);
        c[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.07f, 0.08f, 0.99f);
        c[ImGuiCol_Border] = ImVec4(0.48f, 0.54f, 0.58f, 0.90f);
        accent = ImVec4(0.95f, 0.62f, 0.12f, 1); // Okabe-Ito orange
        selected = ImVec4(0.00f, 0.45f, 0.70f, 1); // Okabe-Ito blue
    } else {
        c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.95f, 1);
        c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1);
        c[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.11f, 0.115f, 1);
        c[ImGuiCol_ChildBg] = ImVec4(0.125f, 0.13f, 0.135f, 1);
        c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.98f);
        c[ImGuiCol_Border] = ImVec4(0.35f, 0.35f, 0.40f, 0.80f);
    }
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.10f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.18f, 0.20f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.23f, 0.25f, 1);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.13f, 0.14f, 1);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.19f, 0.20f, 1);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.10f, 0.11f, 1);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.06f, 0.07f, 1);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.35f, 0.37f, 1);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.48f, 0.49f, 0.51f, 1);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.62f, 0.63f, 0.65f, 1);
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.21f, 0.22f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.31f, 0.32f, 1);
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = ImVec4(0.22f, 0.23f, 0.24f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.32f, 0.33f, 0.34f, 1);
    c[ImGuiCol_HeaderActive] = selected;
    c[ImGuiCol_Separator] = ImVec4(0.28f, 0.29f, 0.31f, 1);
    c[ImGuiCol_SeparatorHovered] = selected;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.14f, 1);
    c[ImGuiCol_TabHovered] = selected;
    c[ImGuiCol_TabSelected] = selected;
    c[ImGuiCol_TabDimmed] = ImVec4(0.09f, 0.09f, 0.11f, 1);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.16f, 0.16f, 0.18f, 1);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.15f, 0.18f, 1);
    c[ImGuiCol_TableBorderStrong] = c[ImGuiCol_Border];
    c[ImGuiCol_TableBorderLight] = ImVec4(c[ImGuiCol_Border].x, c[ImGuiCol_Border].y, c[ImGuiCol_Border].z, 0.55f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
    c[ImGuiCol_NavCursor] = accent;

    style.WindowRounding = style.ChildRounding = style.FrameRounding = 2.0f;
    style.PopupRounding = style.GrabRounding = style.TabRounding = style.ScrollbarRounding = 2.0f;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.TabBarBorderSize = 2.0f;
    style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = theme == UiTheme::HighContrast ? 1.0f : 0.0f;
    style.ScaleAllSizes(dpiScale);
}
