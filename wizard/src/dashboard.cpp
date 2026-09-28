#include "dashboard.h"
#include "logging.h"
void DrawDashboard(const MonitorData& m,bool connected,bool monitoring,float age,DashboardLayout&,bool) {
    const bool fresh=connected && monitoring && m.valid && age<1.0f;
    if(!fresh) ImGui::TextDisabled("Live data unavailable or stale");
    ImGui::Text("Sample age: %.2f s",age);
    if(fresh) {
        ImGui::TextColored(m.synced()?ImVec4(.3f,.9f,.5f,1):ImVec4(1,.4f,.3f,1),
                           "Sync: %s    Sync losses: %u%s",m.synced()?"Acquired":"Not acquired",m.lossOfSyncCount,m.lossOfSyncCount==65535?"+":"");
        if(m.unsaved()) ImGui::TextColored(ImVec4(1,.8f,.2f,1),"Unsaved tune in ECU RAM");
        if(m.flags&32) ImGui::TextUnformatted("Saving tune");
        ImGui::Text("Fuel cut: %s  Spark cut: %s  Rev limit: %s  MIL: %s",
                    (m.flags&2)?"Yes":"No",(m.flags&4)?"Yes":"No",(m.flags&1)?"Yes":"No",(m.flags&64)?"On":"Off");
        ImGui::Text("Cranking: %s  Running: %s  Overrun cut: %s  Closed loop: %s",
                    (m.state&2)?"Yes":"No",(m.state&4)?"Yes":"No",(m.state&8)?"Yes":"No",(m.state&16)?"Yes":"No");
        ImGui::Text("Fan: %s  Pump: %s  Launch: %s  Anti-lag: %s",
                    (m.state&32)?"On":"Off",(m.state&64)?"On":"Off",(m.state&128)?"On":"Off",(m.flags&8)?"On":"Off");
        ImGui::Text("Output inhibits: 0x%04X",m.inhibits);
    }
    ImGui::Separator();
    if(ImGui::BeginTable("Live signals",3,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Signal"); ImGui::TableSetupColumn("Value"); ImGui::TableSetupColumn("Unit"); ImGui::TableHeadersRow();
        for(int i=0;i<SIG_COUNT;++i) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(SIGNAL_DEFS[i].name);
            ImGui::TableNextColumn();
            if(!fresh || (i==SIG_AFR && !m.measuredAfr)) ImGui::TextDisabled("--");
            else ImGui::Text("%.2f",LogState::getSignalValue(LogSignal(i),m));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(SIGNAL_DEFS[i].unit);
        }
        ImGui::EndTable();
    }
}
void DrawInlineDashboard(const MonitorData& m,bool connected,bool monitoring,float age,bool) {
    if(!connected || !monitoring || !m.valid || age>=1) { ImGui::TextDisabled("Live data unavailable"); return; }
    ImGui::Text("%u RPM | %.1f%% TPS | %u kPa | %d C | %.1f V | Sync %s | Losses %u",
                m.rpm,m.tps,m.kpa,m.clt,m.battery,m.synced()?"OK":"lost",m.lossOfSyncCount);
}
