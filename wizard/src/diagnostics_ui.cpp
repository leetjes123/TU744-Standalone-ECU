#include "app.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#ifdef TW_UI_PREVIEW
extern ImVec2 diagnosticPreviewCardCenter;
#endif

namespace {
const ImVec4 cyan(.34f,.79f,.98f,1), amber(1,.74f,.31f,1);
void Heading(const char* text) {
    ImGui::SetWindowFontScale(1.55f); ImGui::TextUnformatted(text); ImGui::SetWindowFontScale(1);
}
void Tile(const char* id,const char* label,const char* value,const char* caption,ImVec4 accent) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,S(9));
    ImGui::BeginChild(id,ImVec2(0,S(113)),ImGuiChildFlags_Borders);
    ImGui::TextDisabled("%s",label);
    const float scale=std::min(1.75f,ImGui::GetContentRegionAvail().x/std::max(1.f,ImGui::CalcTextSize(value).x));
    ImGui::SetWindowFontScale(scale); ImGui::TextColored(accent,"%s",value); ImGui::SetWindowFontScale(1);
    ImGui::TextDisabled("%s",caption);
    ImGui::EndChild(); ImGui::PopStyleVar();
}
void Metric(const char* label,const char* value,const char* units) {
    Tile(label,label,value,units,ImGui::GetStyleColorVec4(ImGuiCol_Text));
}
bool Matches(const std::string& a,const char* query) {
    std::string hay=a,needle=query;
    for(auto& c:hay) c=char(std::tolower((unsigned char)c));
    for(auto& c:needle) c=char(std::tolower((unsigned char)c));
    return hay.find(needle)!=std::string::npos;
}
bool FaultCard(const char* id,const std::string& code,const char* description,bool active,unsigned occurrences,bool freeze) {
    ImGui::PushID(id);
    float width=ImGui::GetContentRegionAvail().x;
    const float wrap=std::max(S(100),width-S(36));
    float descHeight=ImGui::CalcTextSize(description,nullptr,false,wrap).y;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,S(9));
    ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
    bool click=ImGui::Button("##fault",ImVec2(width,S(80)+descHeight));
    ImGui::PopStyleColor(); ImGui::PopStyleVar();
    ImVec2 p=ImGui::GetItemRectMin(),end=ImGui::GetItemRectMax();
#ifdef TW_UI_PREVIEW
    if(!strcmp(id,"event67")) diagnosticPreviewCardCenter=ImVec2(p.x+S(50),p.y+S(25));
#endif
    auto* d=ImGui::GetWindowDrawList();
    const ImU32 accent=ImGui::ColorConvertFloat4ToU32(active?amber:cyan);
    d->AddRect(p,end,ImGui::GetColorU32(ImGuiCol_Border),S(9));
    d->AddRectFilled(ImVec2(p.x+S(1),p.y+S(14)),ImVec2(p.x+S(4),end.y-S(14)),accent,S(2));
    d->AddText(ImGui::GetFont(),ImGui::GetFontSize()*1.3f,ImVec2(p.x+S(17),p.y+S(10)),accent,code.c_str());
    const char* state=active?"CURRENT":"STORED";
    d->AddText(ImVec2(end.x-ImGui::CalcTextSize(state).x-S(18),p.y+S(14)),accent,state);
    d->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(p.x+S(17),p.y+S(40)),ImGui::GetColorU32(ImGuiCol_Text),description,nullptr,wrap);
    char foot[150]; snprintf(foot,sizeof(foot),"%u occurrence%s   /   %s",occurrences,occurrences==1?"":"s",freeze?"Open freeze frame  >":"Open controller fault details  >");
    d->AddText(ImVec2(p.x+S(17),end.y-S(27)),ImGui::GetColorU32(ImGuiCol_TextDisabled),foot);
    ImGui::PopID(); return click;
}
}
void App::openToolTab(OpenTab::SpecialType type) {
    for(int i=0;i<numTabs;++i) if(tabs[i].open && tabs[i].type==type) { activeTab=i; return; }
    int i=allocateTab(); if(i<0) return;
    tabs[i].open=true; tabs[i].type=type; activeTab=i;
}
void App::beginDiagnostics(bool clear) {
    if(ecuBusy() || !serial.isOpen()) return;
    if(!ecu.stopMonitor()) { snprintf(statusLine,sizeof(statusLine),"Could not pause monitoring for diagnostics"); return; }
    liveTuning=false; autotune.clear(); autotune.collecting=false; ecu.clearMonitorUpdates();
    diagnostics.start(clear,GetTickCount64());
}
void App::drawDiagnostics() {
    auto& s=diagnostics.snapshot;
    const bool connected=serial.isOpen();
    const bool compact=ImGui::GetContentRegionAvail().x<S(600);
    const bool stale=s.valid && (!connected || diagnostics.phase==Diagnostics::Phase::Failed || diagnostics.busy());
    Heading("Engine diagnostics");
    ImGui::TextDisabled("FAULT MEMORY  /  RECORDED ENGINE CONDITIONS");
    ImGui::Spacing();
    ImGui::BeginDisabled(ecuBusy() || !connected);
    if(ImGui::Button(diagnostics.busy()?"Reading...":"Read fault memory",ImVec2(S(168),S(36)))) beginDiagnostics();
    ImGui::SameLine();
    if(ImGui::Button("Clear DTCs...",ImVec2(S(135),S(36)))) ImGui::OpenPopup("Clear fault memory?");
    ImGui::EndDisabled();
    if(!compact) ImGui::SameLine();
    if(s.valid) ImGui::TextDisabled("%s / read %.0f s ago",stale?"Previous snapshot":"Snapshot",(GetTickCount64()-s.capturedAt)/1000.0);
    else ImGui::TextDisabled("%s",connected?"Ready to scan":"Disconnected");
    if(diagnostics.busy()) ImGui::ProgressBar(diagnostics.progress(),ImVec2(-1,S(4)),"");
    ImGui::Spacing();
    unsigned active=0; for(const auto& r:s.records) active+=r.active;
    for(const auto& r:s.controller) active+=r.active;
    char total[20],current[20]; snprintf(total,sizeof(total),"%zu",s.records.size()+s.controller.size()); snprintf(current,sizeof(current),"%u",active);
    if(compact) {
        ImGui::TextColored(s.mil||s.flashing?amber:cyan,"MIL demand: %s",s.valid?(s.flashing?"Flashing":s.mil?"On":"Off"):"Unknown");
        ImGui::Text("Stored: %s    Currently failing: %s",s.valid?total:"--",s.valid?current:"--");
    } else if(ImGui::BeginTable("Scan summary",3,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); Tile("MIL","MALFUNCTION LAMP",s.valid?(s.flashing?"FLASHING":s.mil?"ON":"OFF"):"--","ECU request at last scan",s.mil||s.flashing?amber:cyan);
        ImGui::TableNextColumn(); Tile("Stored","STORED RECORDS",s.valid?total:"--","DTCs + controller faults",cyan);
        ImGui::TableNextColumn(); Tile("Current","CURRENTLY FAILING",s.valid?current:"--","Within stored records",active?amber:cyan);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if(diagnostics.phase==Diagnostics::Phase::Failed || diagnostics.clearResult==Diagnostics::ClearResult::Unknown)
        ImGui::PushStyleColor(ImGuiCol_Text,amber);
    ImGui::TextWrapped("%s",diagnostics.message.c_str());
    if(diagnostics.phase==Diagnostics::Phase::Failed || diagnostics.clearResult==Diagnostics::ClearResult::Unknown) ImGui::PopStyleColor();
    if(s.valid && (s.clearPending || s.dirty)) ImGui::TextDisabled("History changes await persistence at key-off.");
    if(s.valid && (s.journalResult==2 || s.journalResult==3)) ImGui::TextColored(amber,"History storage error reported at last scan.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(compact?-1:std::max(S(140),ImGui::GetContentRegionAvail().x-S(220)));
    ImGui::InputTextWithHint("##Fault search","Search code or description...",diagnosticSearch,sizeof(diagnosticSearch));
    if(!compact) ImGui::SameLine(); ImGui::SetNextItemWidth(compact?-1:S(205));
    ImGui::Combo("##Fault filter",&diagnosticFilter,"All stored records\0Currently failing\0Stored, not failing\0");
    ImGui::BeginChild("Fault results",ImVec2(0,0));
    int visible=0;
    if(!s.valid) {
        ImGui::Spacing(); ImGui::Spacing(); Heading("Fault memory has not been read");
        ImGui::TextWrapped("Connect to the ECU and select Read fault memory. Calibration does not need to be loaded.");
    } else if(s.records.empty() && s.controller.empty()) {
        ImGui::Spacing(); ImGui::Spacing(); Heading("No stored faults in this scan");
        ImGui::TextWrapped("Faults can return while their conditions are present. Read again after checking the engine.");
    } else {
        auto show=[&](bool a) { return diagnosticFilter==0 || (diagnosticFilter==1?a:!a); };
        ImGui::SeparatorText("Diagnostic trouble codes");
        if(s.records.empty()) ImGui::TextDisabled("No stored DTCs.");
        for(const auto& r:s.records) if(show(r.active) && Matches(r.code+" "+r.description,diagnosticSearch)) {
            ++visible; char id[24]; snprintf(id,sizeof(id),"event%u",r.event);
            if(FaultCard(id,r.code,r.description.c_str(),r.active,r.occurrences,true)) { selectedDtc=r; diagnosticPopup=true; }
        }
        if(!s.controller.empty()) ImGui::SeparatorText("Controller faults");
        for(const auto& r:s.controller) if(show(r.active)) {
            char id[30]; snprintf(id,sizeof(id),"Controller %02u",r.id);
            if(!Matches(std::string(id)+" "+ControllerFaultName(r.id),diagnosticSearch)) continue;
            ++visible;
            if(FaultCard(id,id,ControllerFaultName(r.id),r.active,r.occurrences,false)) { selectedControllerFault=r; controllerPopup=true; }
        }
        if(!visible) ImGui::TextDisabled("No records match this filter.");
    }
    ImGui::EndChild();
    if(diagnosticPopup) { ImGui::OpenPopup("Freeze frame"); diagnosticPopup=false; }
    auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(S(820),viewport->WorkSize.x-S(35)),0),ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(320),S(200)),ImVec2(viewport->WorkSize.x-S(30),viewport->WorkSize.y-S(30)));
    if(ImGui::BeginPopupModal("Freeze frame",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& r=selectedDtc;
        ImGui::TextColored(cyan,"RECORDED SNAPSHOT  /  FIRST CONFIRMATION");
        Heading(r.firstCode.c_str()); ImGui::TextWrapped("%s",LookupDtc(r.event,r.firstSubtype).description?LookupDtc(r.event,r.firstSubtype).description:"Unmapped diagnostic event");
        ImGui::TextDisabled("Latest code: %s  /  %s  /  %u occurrences",r.code.c_str(),r.active?"Currently failing":"Stored, not currently failing",r.occurrences);
        ImGui::Spacing();
        const int columns=ImGui::GetContentRegionAvail().x>S(600)?3:2;
        if(ImGui::BeginTable("Freeze measurements",columns,ImGuiTableFlags_SizingStretchSame)) {
            char value[50];
            auto metric=[&](const char* label,const char* fmt,double v,const char* units) {
                ImGui::TableNextColumn(); snprintf(value,sizeof(value),fmt,v); Metric(label,value,units);
            };
            metric("ENGINE SPEED","%.0f",r.rpm,"rpm");
            metric("MANIFOLD PRESSURE","%.0f",r.map,"kPa absolute");
            metric("THROTTLE POSITION","%.1f",r.throttle,"percent");
            metric("COOLANT","%.0f",r.coolant,"degrees C");
            metric("INTAKE AIR","%.0f",r.intake,"degrees C");
            metric("BATTERY","%.1f",r.battery,"V");
            metric("VEHICLE SPEED","%.0f",r.speed,"km/h");
            ImGui::TableNextColumn(); snprintf(value,sizeof(value),"%.1f",r.trim);
            Metric("FUEL CORRECTION",r.trimAvailable?value:"Unavailable","percent / 100 = neutral");
            ImGui::TableNextColumn(); const char* mode=r.mode==0?"Stopped":r.mode==1?"Cranking":r.mode==2?"Running":"Unknown";
            Metric("ENGINE STATE",mode,"At first confirmation");
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextWrapped("These are stored values, not live readings. Later occurrences keep this freeze frame and update the occurrence count and ECU on-time clock.");
        ImGui::TextDisabled("Latest occurrence clock: approximately %.1f minutes of ECU on-time",r.clock*36.1/60);
        if(ImGui::Button("Close freeze frame",ImVec2(S(185),S(35)))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(controllerPopup) { ImGui::OpenPopup("Controller fault details"); controllerPopup=false; }
    ImGui::SetNextWindowSize(ImVec2(std::min(S(600),viewport->WorkSize.x-S(35)),0),ImGuiCond_Appearing);
    if(ImGui::BeginPopupModal("Controller fault details",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& r=selectedControllerFault;
        Heading(ControllerFaultName(r.id));
        ImGui::Text("Controller %02u  /  %s",r.id,r.active?"Currently failing":"Stored");
        ImGui::Text("Reason: %u  /  Occurrences: %u",r.reason,r.occurrences);
        ImGui::Text("First / latest recorded tick: %u / %u ms",r.first,r.last);
        ImGui::TextWrapped("This controller-specific record has no engine freeze frame. Tick values are ECU uptime timestamps from the recorded occurrences; retained history may cross restarts.");
        if(ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopupModal("Clear fault memory?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Keep the key on and stop the engine before clearing.");
        ImGui::TextUnformatted("Stored DTCs and their freeze frames will be erased.");
        ImGui::TextUnformatted("Readiness restarts. Present faults may return; active controller faults remain.");
        ImGui::TextUnformatted("History changes are saved at key-off.");
        ImGui::BeginDisabled(ecuBusy() || !serial.isOpen());
        if(ImGui::Button("Clear and read again",ImVec2(S(190),S(35)))) { beginDiagnostics(true); ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled(); ImGui::SameLine();
        if(ImGui::Button("Cancel",ImVec2(S(95),S(35)))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
void App::drawAutotune() {
    Heading("Autotune VE");
    ImGui::TextDisabled("WIDEBAND FEEDBACK  /  REVIEWABLE CORRECTIONS");
    ImGui::Spacing();
    const bool ready=!ecuBusy() && serial.isOpen() && ecuSynced && cal.loaded && !updateOnly;
    if(!ready) ImGui::TextWrapped("Connect and read or write the active calibration before collecting samples.");
    ImGui::BeginDisabled(!ready);
    ImGui::Checkbox("Collect samples",&autotune.collecting);
    ImGui::SetNextItemWidth(S(280));
    if(ImGui::SliderFloat("Exhaust delay (s)",&autotune.delaySeconds,0,2)) autotune.clear();
    ImGui::TextWrapped("%s",autotune.status);
    ImGui::TextWrapped("Requires a wideband, closed-loop trim off, a warm engine, steady throttle and RPM, and no enrichment or cuts. Confirm exhaust delay for your installation.");
    CalBuffer proposed; const int changed=cal.loaded?autotune.propose(cal,proposed):0;
    ImGui::SeparatorText("VE sample coverage");
    ImGui::TextDisabled("Load rows / RPM columns. Brighter cells have more accepted samples.");
    float cell=std::min(S(31),ImGui::GetContentRegionAvail().x/16);
    auto p=ImGui::GetCursorScreenPos(); auto* d=ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("##Coverage",ImVec2(cell*16,cell*16));
    for(int row=0;row<16;++row) for(int col=0;col<16;++col) {
        int i=row*16+col; const auto& c=autotune.cells[i];
        float coverage=std::min(1.f,float(c.weight)/10);
        ImVec2 lo(p.x+col*cell,p.y+row*cell),hi(lo.x+cell-S(2),lo.y+cell-S(2));
        d->AddRectFilled(lo,hi,ImGui::ColorConvertFloat4ToU32(ImVec4(.10f+.10f*coverage,.16f+.46f*coverage,.20f+.62f*coverage,1)),S(3));
        if(changed && proposed.data[i]!=cal.data[i]) d->AddRect(lo,hi,ImGui::ColorConvertFloat4ToU32(amber),S(3),0,S(2));
        if(ImGui::IsMouseHoveringRect(lo,hi)) {
            ImGui::BeginTooltip(); ImGui::Text("Load row %d / RPM column %d",row,col);
            ImGui::Text("Samples: %u / weighted: %.1f",c.samples,c.weight);
            if(cal.loaded) ImGui::Text("VE: %u -> %u",cal.data[i],proposed.data[i]);
            ImGui::EndTooltip();
        }
    }
    ImGui::Text("%d cells ready / minimum 20 samples and 10 weighted samples per cell",changed);
    ImGui::TextWrapped("Proposals are limited to 2 percent per application. Amber outlines mark cells with a proposal.");
    if(changed && ImGui::CollapsingHeader("Review proposed cells")) {
        if(ImGui::BeginTable("VE proposals",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollY,ImVec2(0,S(170)))) {
            for(const char* name:{"Load row","RPM column","Current VE","Proposed VE","Change"}) ImGui::TableSetupColumn(name);
            ImGui::TableHeadersRow();
            for(int i=0;i<256;++i) if(proposed.data[i]!=cal.data[i]) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%d",i/16);
                ImGui::TableNextColumn(); ImGui::Text("%d",i%16);
                ImGui::TableNextColumn(); ImGui::Text("%u",cal.data[i]);
                ImGui::TableNextColumn(); ImGui::Text("%u",proposed.data[i]);
                ImGui::TableNextColumn(); ImGui::Text("%+.1f%%",(int(proposed.data[i])-int(cal.data[i]))*100.f/cal.data[i]);
            }
            ImGui::EndTable();
        }
    }
    ImGui::BeginDisabled(!changed);
    if(ImGui::Button("Apply proposals locally",ImVec2(S(200),S(35)))) {
        undo.record(cal.data,0,256,"Autotune VE proposal"); memcpy(cal.data,proposed.data,256);
        cal.recomputeDirty(); undo.finalize(cal.data,0,256); autotune.clear();
    }
    ImGui::EndDisabled(); ImGui::SameLine(); if(ImGui::Button("Clear samples",ImVec2(0,S(35)))) autotune.clear();
    ImGui::TextWrapped("Write active tune or Live tuning sends your applied changes. Saving to ECU flash remains a separate action.");
    ImGui::EndDisabled();
}
