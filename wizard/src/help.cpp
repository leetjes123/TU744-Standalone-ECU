#include "help.h"
#include "imgui.h"
#include <algorithm>
extern float g_dpiScale;
bool DrawHelpBook(HelpBook& help) {
    if(!help.active) return false;
    const ImVec2 area=ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(760*g_dpiScale,area.x*.85f),std::min(600*g_dpiScale,area.y*.85f)),ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(400*g_dpiScale,area.x*.85f),std::min(250*g_dpiScale,area.y*.85f)),area);
    if(ImGui::Begin("Tuning Wizard reference",&help.active)) {
        ImGui::TextWrapped("Open a 3072-byte schema-5 calibration, or read the connected ECU. Hover over a setting for its firmware-defined units and accepted range. Map rows are load and columns are RPM. The load axis follows speed-density or alpha-N mode; boost uses TPS.");
        ImGui::Separator();
        ImGui::TextWrapped("Write active tune applies changes to RAM. Map cells use live writes. Other settings use an atomic begin/write/validate/commit transaction. Structural settings require a stopped engine. Readback confirms each completed transfer. Live tuning sends map edits only while the ECU baseline is synchronized.");
        ImGui::TextWrapped("Save tune to ECU flash preserves the active tune. Stop the engine first. Saving latches service mode with outputs off: a key cycle is required afterwards. A failed or interrupted transfer requires reading the ECU again before continuing.");
        ImGui::Separator();
        ImGui::TextWrapped("The standalone fuel calculation starts with required fuel per 720 degrees, scaled by VE/100, MAP/100 in speed-density mode, and 273.15/(IAT C + 273.15). When enabled, stoichiometric AFR/target AFR also scales fuel. During running, warm-up, after-start, acceleration and applied trim multiply fuel; cranking omits those enrichments. Paired injection splits the cycle fuel between events; dead time and duty limits apply afterwards. See firmware/docs/FUELING.md for the exact integer calculation.");
        ImGui::TextWrapped("Live pulse width and advance are planned values. Admission, output inhibits and cuts can prevent the outputs. Measured AFR is unavailable when its value is zero; target AFR is never substituted. Sync shows current acquisition; sync losses are the firmware counter since reset, saturated at 65535 in the live frame. Logs retain both.");
        ImGui::Separator();
        ImGui::TextWrapped("Supplied functional reference: Bosch technical training 1.3.277, September 2000. PDF p.15 (printed 9): MAP/IAT; PDF p.17 (printed 11): 60-2 engine-speed reference; PDF p.26 (printed 20): coolant; PDF p.39 (printed 33): upstream oxygen voltage polarity and equipment variants. These describe vehicle functions, not this standalone firmware's addresses, algorithms or thresholds. M7.4.4 and ME7.4.4 equipment must not be assumed interchangeable.");
    }
    ImGui::End(); return help.active;
}
