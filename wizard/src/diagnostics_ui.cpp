#include "app.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <vector>
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
    liveTuning=false; autotune.resetHistory(); autotune.collecting=false; autotune.settings.autoApply=false; ecu.clearMonitorUpdates();
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
    const ImVec4 good(.38f,.86f,.50f,1), bad(1,.45f,.42f,1), violet(.72f,.55f,1,1);
    auto& s=autotune.settings;
    const AutoTuneContext ctx=autotuneContext();
    const MonitorData& live=ecu.monitor;
    const FeedbackMode mode=autotune.resolveMode(live,ctx);
    const bool narrowband=mode==FeedbackMode::NarrowbandTrim;
    const unsigned char* tune=ecuSynced ? ecuData : cal.data;
    Heading("Autotune VE");
    ImGui::TextDisabled("%s  /  REVIEWABLE, BOUNDED CORRECTIONS",FeedbackModeName(mode));
    ImGui::Spacing();
    const bool connected=!ecuBusy() && serial.isOpen() && ecuSynced && cal.loaded && !updateOnly;
    if(!cal.loaded) ImGui::TextWrapped("Open or read a calibration first.");
    else if(!connected) ImGui::TextWrapped("Connect and read or write the active calibration to collect live samples. Log analysis works offline against the loaded calibration.");
    ImGui::BeginDisabled(!connected);
    if(ImGui::Checkbox("Collect samples",&autotune.collecting) && autotune.collecting && !autotune.baselineValid) autotune.setBaseline(ecuData);
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::TextDisabled("%s",autotune.collecting?autotune.status:"Collection is off");

    // Requirements checklist for the detected mode, from the latest live frame.
    if(connected) {
        ImGui::SeparatorText("Requirements");
        const bool running=live.valid && live.synced() && (live.state&4) && !(live.state&(2|8|128));
        const bool enrich=(s.allowWarmup||live.warmup==100) && (s.allowAfterstart||live.afterstart==100) && (s.allowAccel||live.accel==100);
        const bool steady=autotune.lastReject!=AutoTuneReject::Transient && autotune.lastReject!=AutoTuneReject::Settling;
        char warm[48],window[64];
        snprintf(warm,sizeof(warm),"Warm (coolant %d / %.0f C)",live.clt,s.cltMin);
        snprintf(window,sizeof(window),"Target in window (%.1f-%.1f AFR)",ctx.windowMinAfr,ctx.windowMaxAfr);
        std::vector<std::pair<const char*,bool>> items={
            {"Calibration matches ECU",ctx.calibrationMatches},
            {narrowband?"Oxygen input: narrowband":"Oxygen input: wideband",narrowband?ctx.oxygenInput==0:ctx.oxygenInput==1},
            {"Running, synced, no DFCO",running},
            {"No cuts, limits or inhibits",live.valid && !(live.flags&0xAF) && !live.inhibits},
            {warm,live.clt>=s.cltMin},{"Enrichments neutral",enrich},{"Steady",steady}};
        if(narrowband) {
            items.push_back({"STFT enabled",(ecuData[0x5D4]&0x20)!=0});
            items.push_back({"Trim active now",(live.state&16)!=0});
            items.push_back({window,live.targetAfr>=ctx.windowMinAfr-0.05f && live.targetAfr<=ctx.windowMaxAfr+0.05f});
            items.push_back({"Narrowband switching",live.narrowbandBand!=0});
        } else {
            items.push_back({"AFR reading valid",live.measuredAfr>0});
            if(mode==FeedbackMode::WidebandOpenLoop) items.push_back({"Closed-loop trim off",!(live.state&16)});
        }
        const int columns=std::max(1,int(ImGui::GetContentRegionAvail().x/S(250)));
        if(ImGui::BeginTable("Requirements",columns)) {
            for(const auto& item:items) {
                ImGui::TableNextColumn();
                ImGui::TextColored(item.second?good:bad,"%s",item.second?"OK":"--"); ImGui::SameLine(); ImGui::TextUnformatted(item.first);
            }
            ImGui::EndTable();
        }
    }
    {
        std::vector<int> reasons;
        for(int i=0;i<int(AutoTuneReject::Count);++i) if(autotune.rejects[i]) reasons.push_back(i);
        std::sort(reasons.begin(),reasons.end(),[&](int a,int b) { return autotune.rejects[a]>autotune.rejects[b]; });
        char label[96]; snprintf(label,sizeof(label),"Samples: %u accepted, %zu rejection reasons###Rejections",autotune.accepted,reasons.size());
        if(ImGui::TreeNode(label)) {
            if(reasons.empty()) ImGui::TextDisabled("No samples rejected.");
            else if(ImGui::BeginTable("Rejections",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
                for(int i:reasons) {
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(AutoTuneRejectName(AutoTuneReject(i)));
                    ImGui::TableNextColumn(); ImGui::Text("%u",autotune.rejects[i]);
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
    }

    if(ImGui::CollapsingHeader("Settings")) {
        const float width=S(260);
        auto dragF=[&](const char* label,float* v,float speed,float lo,float hi,const char* fmt) {
            ImGui::SetNextItemWidth(width); ImGui::DragFloat(label,v,speed,lo,hi,fmt,ImGuiSliderFlags_AlwaysClamp);
        };
        int modeIndex=int(s.mode); ImGui::SetNextItemWidth(width);
        if(ImGui::Combo("Feedback mode",&modeIndex,"Auto (from oxygen input and trim)\0Wideband, open loop\0Wideband, trim compensated\0Narrowband trim learning\0"))
            s.mode=FeedbackMode(modeIndex);
        ImGui::TextUnformatted("Preset:"); ImGui::SameLine();
        if(ImGui::SmallButton("Conservative")) s.applyPreset(AutoTuneSettings::Preset::Conservative); ImGui::SameLine();
        if(ImGui::SmallButton("Normal")) s.applyPreset(AutoTuneSettings::Preset::Normal); ImGui::SameLine();
        if(ImGui::SmallButton("Aggressive")) s.applyPreset(AutoTuneSettings::Preset::Aggressive);
        ImGui::SeparatorText("Lambda delay");
        dragF("Base delay (s)",&s.delayBase,0.005f,0,2,"%.3f");
        dragF("Engine cycles",&s.delayCycles,0.1f,0,20,"%.1f");
        ImGui::TextDisabled("Delay: %.2f s at 800 rpm, %.2f s at 3000 rpm, %.2f s at 6000 rpm",autotune.delayFor(800),autotune.delayFor(3000),autotune.delayFor(6000));
        ImGui::SeparatorText("Sample filters");
        ImGui::SetNextItemWidth(width); ImGui::DragFloatRange2("RPM",&s.rpmMin,&s.rpmMax,10,0,10000,"%.0f","%.0f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(width); ImGui::DragFloatRange2(ctx.alphaN?"Load (TPS %)":"Load (MAP kPa)",&s.loadMin,&s.loadMax,1,0,255,"%.0f","%.0f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(width); ImGui::DragFloatRange2("TPS (%)",&s.tpsMin,&s.tpsMax,0.5f,0,100,"%.1f","%.1f",ImGuiSliderFlags_AlwaysClamp);
        dragF("Coolant minimum (C)",&s.cltMin,0.5f,-40,120,"%.0f");
        dragF("Intake air maximum (C)",&s.iatMax,0.5f,-40,150,"%.0f");
        dragF("TPS rate maximum (%/s)",&s.tpsRateMax,0.1f,0.1f,100,"%.1f");
        dragF("RPM rate maximum (rpm/s)",&s.rpmRateMax,5,10,5000,"%.0f");
        dragF("MAP rate maximum (kPa/s)",&s.mapRateMax,0.5f,1,500,"%.0f");
        dragF("Settle time after transient (s)",&s.settleTime,0.05f,0,5,"%.2f");
        ImGui::Checkbox("Exclude idle",&s.excludeIdle);
        ImGui::Checkbox("Allow warm-up enrichment",&s.allowWarmup); ImGui::SameLine();
        ImGui::Checkbox("Allow after-start",&s.allowAfterstart); ImGui::SameLine();
        ImGui::Checkbox("Allow AE",&s.allowAccel);
        if(s.allowWarmup||s.allowAfterstart||s.allowAccel) ImGui::TextColored(amber,"Enrichment errors will be learned into VE. Off is recommended.");
        ImGui::SetNextItemWidth(width); ImGui::DragFloatRange2("Plausible AFR (wideband)",&s.afrMin,&s.afrMax,0.1f,5,25,"%.1f","%.1f",ImGuiSliderFlags_AlwaysClamp);
        dragF("Maximum per-sample error (%)",&s.maxErrorPercent,0.5f,1,50,"%.1f");
        dragF("Minimum cell weight per sample",&s.minCellWeight,0.01f,0,1,"%.2f");
        ImGui::SameLine(); if(ImGui::SmallButton("Centre only")) s.minCellWeight=0.5f;
        ImGui::SetNextItemWidth(width); ImGui::DragInt("Narrowband minimum switches",&s.minSwitches,0.1f,2,40,"%d",ImGuiSliderFlags_AlwaysClamp);
        dragF("Trim limit margin (%)",&s.trimLimitMargin,0.1f,0,10,"%.1f");
        ImGui::SeparatorText("Confidence");
        ImGui::SetNextItemWidth(width); ImGui::DragInt("Minimum hits",&s.minHits,0.2f,1,500,"%d",ImGuiSliderFlags_AlwaysClamp);
        dragF("Minimum weight",&s.minWeight,0.1f,0.1f,200,"%.1f");
        dragF("Maximum std deviation (%)",&s.maxStdDevPercent,0.1f,0.1f,20,"%.1f");
        ImGui::Checkbox("Reject outliers (3 sigma after 10 hits)",&s.outlierRejection);
        ImGui::SeparatorText("Proposal limits");
        const float presets[]={1,0.5f,0.25f,0.1f};
        int resistance=4; for(int i=0;i<4;++i) if(std::abs(s.resistance-presets[i])<1e-4f) resistance=i;
        ImGui::SetNextItemWidth(width);
        if(ImGui::Combo("Change resistance",&resistance,"Easy (1.0)\0Normal (0.5)\0Hard (0.25)\0Very hard (0.1)\0Custom\0") && resistance<4) s.resistance=presets[resistance];
        if(resistance==4) { ImGui::SetNextItemWidth(width); ImGui::SliderFloat("Custom resistance",&s.resistance,0.05f,1,"%.2f",ImGuiSliderFlags_AlwaysClamp); }
        ImGui::SetNextItemWidth(width); ImGui::SliderFloat("Authority from baseline (+/- %)",&s.authorityPercent,1,100,"%.0f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(width); ImGui::SliderFloat("Max step per update (%, 0 = off)",&s.maxStepPercent,0,25,"%.1f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(width); ImGui::DragIntRange2("VE bounds",&s.veMin,&s.veMax,1,0,255,"%d","%d",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(width); ImGui::SliderFloat("Deadband (%)",&s.deadbandPercent,0,5,"%.1f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("Smooth with ready neighbours",&s.smoothing);
        ImGui::SeparatorText("Advanced");
        ImGui::Checkbox("Show auto-apply",&s.advanced);
        if(ImGui::Button("Restore defaults")) { const bool advanced=s.advanced; s=AutoTuneSettings{}; s.advanced=advanced; }
    }

    // Proposal for the local calibration, with per-cell detail for the views.
    std::array<AutoTuneCellProposal,256> detail{};
    CalBuffer proposed; const int changed=cal.loaded?autotune.propose(cal,proposed,&detail):0;
    auto inWindow=[&](int i) { const float afr=tune[0x200+i]*0.1f; return afr>=ctx.windowMinAfr-0.05f && afr<=ctx.windowMaxAfr+0.05f; };
    ImGui::SeparatorText("VE map");
    const char* views[]={"Coverage","Correction %","Confidence","Proposed VE","Change from baseline","Locks"};
    for(int i=0;i<6;++i) { if(i) ImGui::SameLine(); ImGui::RadioButton(views[i],&autotuneMapView,i); }
    ImGui::TextDisabled("Load rows / RPM columns. Click a cell to lock or unlock it; right-click to select it.");
    if(cal.loaded) {
        const float axis=S(42);
        const float cell=std::max(S(14),std::min(S(38),(ImGui::GetContentRegionAvail().x-axis)/16));
        const ImVec2 origin=ImGui::GetCursorScreenPos(); auto* d=ImGui::GetWindowDrawList();
        const ImVec2 p(origin.x+axis,origin.y);
        ImGui::InvisibleButton("##VE map",ImVec2(axis+cell*16,cell*16+S(18)));
        const bool hovered=ImGui::IsItemHovered();
        const ImVec2 mouse=ImGui::GetIO().MousePos;
        const float textSize=ImGui::GetFontSize()*std::min(1.0f,cell/S(34));
        const ImU32 textColor=ImGui::GetColorU32(ImGuiCol_Text), disabled=ImGui::GetColorU32(ImGuiCol_TextDisabled);
        auto diverging=[](double v,double full) {
            const float t=float(std::min(1.0,std::abs(v)/full));
            return v>0 ? ImVec4(.16f+.70f*t,.17f+.25f*t,.20f,1) : ImVec4(.16f,.20f+.25f*t,.22f+.70f*t,1);
        };
        for(int row=0;row<16;++row) {
            char text[16]; snprintf(text,sizeof(text),"%.0f",cal.readAxisValue(AXIS_LOAD,row));
            d->AddText(ImGui::GetFont(),textSize,ImVec2(origin.x,p.y+row*cell+(cell-textSize)/2),disabled,text);
        }
        for(int col=0;col<16;col+=2) {
            char text[16]; snprintf(text,sizeof(text),"%.0f",cal.readAxisValue(AXIS_RPM,col));
            d->AddText(ImGui::GetFont(),textSize*0.85f,ImVec2(p.x+col*cell,p.y+16*cell+S(2)),disabled,text);
        }
        int hoverCell=-1;
        for(int row=0;row<16;++row) for(int col=0;col<16;++col) {
            const int i=row*16+col; const auto& c=autotune.cells[i]; const auto& q=detail[i];
            const ImVec2 lo(p.x+col*cell,p.y+row*cell),hi(lo.x+cell-S(2),lo.y+cell-S(2));
            ImVec4 fill(.12f,.14f,.17f,1); char text[16]={};
            const double correction=(c.mean()-1)*100;
            switch(autotuneMapView) {
            case 0: { const float t=std::min(1.f,float(c.sumW/std::max(0.1f,s.minWeight)));
                fill=ImVec4(.10f+.10f*t,.16f+.46f*t,.20f+.62f*t,1); if(c.hits) snprintf(text,sizeof(text),"%u",c.hits); break; }
            case 1: if(c.hits) { fill=diverging(correction,10); snprintf(text,sizeof(text),"%+.1f",correction); } break;
            case 2: if(c.hits) {
                const float t=std::min(1.f,std::min(float(c.hits)/std::max(1,s.minHits),float(c.sumW/std::max(0.1f,s.minWeight))));
                const bool noisy=c.stdDev()*100>s.maxStdDevPercent;
                fill=q.ready?ImVec4(.18f,.55f,.30f,1):noisy?ImVec4(.55f,.22f,.22f,1):ImVec4(.16f+.30f*t,.17f+.25f*t,.12f,1);
                snprintf(text,sizeof(text),"%.0f%%",t*100); } break;
            case 3: fill=q.proposed!=q.current?ImVec4(.45f,.33f,.10f,1):fill; snprintf(text,sizeof(text),"%d",q.proposed); break;
            case 4: if(q.baseline) { const double delta=(q.proposed-q.baseline)*100.0/q.baseline;
                fill=diverging(delta,std::max(1.0f,s.authorityPercent)); if(q.proposed!=q.baseline) snprintf(text,sizeof(text),"%+.0f",delta); } break;
            case 5: fill=autotune.locks[i]?ImVec4(.42f,.42f,.46f,1):fill; if(autotune.locks[i]) snprintf(text,sizeof(text),"L"); break;
            }
            d->AddRectFilled(lo,hi,ImGui::ColorConvertFloat4ToU32(fill),S(3));
            if(narrowband && !inWindow(i))
                for(float k=S(5);k<(cell-S(2))*2;k+=S(6)) {
                    const float a=std::min(k,cell-S(2)),b=std::max(0.f,k-(cell-S(2)));
                    d->AddLine(ImVec2(lo.x+b,lo.y+a),ImVec2(lo.x+a,lo.y+b),IM_COL32(0,0,0,90),S(1));
                }
            if(text[0] && cell>=S(22)) {
                const ImVec2 size=ImGui::GetFont()->CalcTextSizeA(textSize*0.8f,FLT_MAX,0,text);
                d->AddText(ImGui::GetFont(),textSize*0.8f,ImVec2((lo.x+hi.x-size.x)/2,(lo.y+hi.y-size.y)/2),textColor,text);
            }
            if(q.atAuthority) d->AddRect(lo,hi,ImGui::ColorConvertFloat4ToU32(bad),S(3),0,S(2));
            else if(q.proposed!=q.current) d->AddRect(lo,hi,ImGui::ColorConvertFloat4ToU32(amber),S(3),0,S(2));
            else if(q.blockedByResolution) d->AddRect(lo,hi,ImGui::ColorConvertFloat4ToU32(violet),S(3),0,S(1.5f));
            if(autotune.locks[i] && autotuneMapView!=5) d->AddRectFilled(ImVec2(hi.x-S(7),lo.y+S(1)),ImVec2(hi.x-S(1),lo.y+S(7)),IM_COL32(200,200,210,230),S(1));
            if(autotuneSelected[i]) d->AddRect(ImVec2(lo.x-S(1),lo.y-S(1)),ImVec2(hi.x+S(1),hi.y+S(1)),IM_COL32(255,255,255,255),S(3),0,S(2));
            if(hovered && mouse.x>=lo.x && mouse.x<lo.x+cell && mouse.y>=lo.y && mouse.y<lo.y+cell) hoverCell=i;
        }
        if(hoverCell>=0) {
            const int i=hoverCell; const auto& c=autotune.cells[i]; const auto& q=detail[i];
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { autotune.locks[i]=!autotune.locks[i]; saveAutotuneLocks(); }
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)) autotuneSelected[i]=!autotuneSelected[i];
            ImGui::BeginTooltip();
            ImGui::Text("Load row %d (%.0f) / RPM column %d (%.0f)",i/16,cal.readAxisValue(AXIS_LOAD,i/16),i%16,cal.readAxisValue(AXIS_RPM,i%16));
            ImGui::Text("Hits %u / weight %.1f / last hit %.0f s ago",c.hits,c.sumW,c.hits?std::max(0.f,elapsed-c.lastHit):0.f);
            if(c.hits) ImGui::Text("Correction %+.2f %% / std dev %.2f %%",(c.mean()-1)*100,c.stdDev()*100);
            ImGui::Text("VE %d -> %d (baseline %d)",q.current,q.proposed,q.baseline);
            ImGui::Text("Target AFR %.1f",tune[0x200+i]*0.1f);
            if(q.ready) ImGui::TextColored(good,"Ready"); else ImGui::TextDisabled("Not ready");
            if(q.locked) ImGui::TextUnformatted("Locked");
            if(q.deadband) ImGui::TextUnformatted("Within deadband");
            if(q.atAuthority) ImGui::TextColored(bad,"At authority limit: check injector data or sensors");
            if(q.blockedByResolution) ImGui::TextColored(violet,"Blocked by resolution: exact %.2f",q.exact);
            if(narrowband && !inWindow(i)) ImGui::TextColored(amber,"Outside narrowband window: needs a wideband");
            ImGui::EndTooltip();
        }
        ImGui::TextDisabled("Amber: proposal. Red: at authority limit. Violet: blocked by VE resolution.%s",narrowband?" Hatched: outside narrowband window.":"");
    }

    int ready=0,atLimit=0,blocked=0;
    for(const auto& q:detail) { ready+=q.ready; atLimit+=q.atAuthority; blocked+=q.blockedByResolution; }
    ImGui::Text("%d cells ready, %d proposals, %d at authority limit, %d blocked by resolution",ready,changed,atLimit,blocked);
    if(ready && ImGui::CollapsingHeader("Review proposed cells")) {
        if(ImGui::BeginTable("VE proposals",9,ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingStretchProp,ImVec2(0,S(200)))) {
            ImGui::TableSetupScrollFreeze(0,1);
            for(const char* name:{"Load row","RPM column","Current","Proposed","Change","Correction","Std dev","Hits","Notes"}) ImGui::TableSetupColumn(name);
            ImGui::TableHeadersRow();
            for(int i=0;i<256;++i) {
                const auto& q=detail[i]; const auto& c=autotune.cells[i];
                if(!q.ready) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%d",i/16);
                ImGui::TableNextColumn(); ImGui::Text("%d",i%16);
                ImGui::TableNextColumn(); ImGui::Text("%d",q.current);
                ImGui::TableNextColumn(); ImGui::Text("%d",q.proposed);
                ImGui::TableNextColumn(); ImGui::Text("%+.1f%%",q.current?(q.proposed-q.current)*100.f/q.current:0.f);
                ImGui::TableNextColumn(); ImGui::Text("%+.2f%%",(c.mean()-1)*100);
                ImGui::TableNextColumn(); ImGui::Text("%.2f%%",c.stdDev()*100);
                ImGui::TableNextColumn(); ImGui::Text("%u",c.hits);
                ImGui::TableNextColumn();
                if(q.locked) ImGui::TextDisabled("Locked");
                else if(q.atAuthority) ImGui::TextColored(bad,"At authority limit");
                else if(q.blockedByResolution) ImGui::TextColored(violet,"Blocked by resolution");
                else if(q.deadband) ImGui::TextDisabled("Deadband");
            }
            ImGui::EndTable();
        }
    }

    ImGui::BeginDisabled(!changed || ecuBusy());
    if(ImGui::Button("Apply proposals locally",ImVec2(S(200),S(35)))) {
        const int n=applyAutotuneProposals(false);
        snprintf(statusLine,sizeof(statusLine),"Applied %d autotune VE proposals locally",n);
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    if(ImGui::Button("Clear all",ImVec2(0,S(35)))) { autotune.clear(); autotuneSelected={}; }
    bool anySelected=false; for(bool b:autotuneSelected) anySelected|=b;
    ImGui::SameLine(); ImGui::BeginDisabled(!anySelected);
    if(ImGui::Button("Clear selected",ImVec2(0,S(35)))) {
        for(int i=0;i<256;++i) if(autotuneSelected[i]) autotune.cells[i]={};
        autotuneSelected={};
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!cal.loaded || ecuBusy());
    if(ImGui::Button("Analyse log...",ImVec2(0,S(35)))) analyseAutotuneLog();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!cal.loaded);
    if(ImGui::Button("Lock all")) { autotune.locks.fill(true); saveAutotuneLocks(); } ImGui::SameLine();
    if(ImGui::Button("Unlock all")) { autotune.locks={}; saveAutotuneLocks(); } ImGui::SameLine();
    if(ImGui::Button("Lock cells outside narrowband window")) {
        for(int i=0;i<256;++i) if(!inWindow(i)) autotune.locks[i]=true;
        saveAutotuneLocks();
    }
    ImGui::SameLine();
    if(ImGui::Button("Reset baseline")) autotune.setBaseline(cal.data);
    ImGui::EndDisabled();
    if(s.advanced) {
        ImGui::SeparatorText("Auto-apply");
        const bool canAuto=connected && liveTuning && autotune.collecting;
        ImGui::BeginDisabled(!canAuto && !s.autoApply);
        if(ImGui::Checkbox("Auto-apply ready proposals",&s.autoApply) && s.autoApply) {
            autotuneApplyMode=s.mode; autotuneApplyOxygen=ecuData[0x600]; autotuneNextApply=elapsed+s.autoApplyInterval;
        }
        ImGui::EndDisabled(); ImGui::SameLine(); ImGui::SetNextItemWidth(S(160));
        ImGui::SliderFloat("Interval (s)",&s.autoApplyInterval,2,120,"%.0f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextWrapped("Needs Live tuning and sample collection. Every interval, ready proposals are applied with an undo entry and Live tuning writes them. The authority limit bounds the total change from the baseline. Stops on disconnect, mode change or a calibration mismatch.");
    }
    if(!autotuneLog.path.empty()) {
        ImGui::SeparatorText("Log analysis");
        ImGui::TextWrapped("%s: %d rows (%d skipped), %u samples accepted",autotuneLog.path.c_str(),autotuneLog.rows,autotuneLog.skippedRows,autotune.accepted);
        bool otherGeneration=false;
        for(auto g:autotuneLog.generations) otherGeneration|=ecuSynced && g!=baselineGeneration;
        if(autotuneLog.generations.size()>1 || otherGeneration)
            ImGui::TextColored(amber,"The log spans %zu calibration generations%s; results may mix different tunes.",autotuneLog.generations.size(),otherGeneration?", not matching the ECU":"");
        if(autotuneLog.runningRows && autotuneLog.veMismatchRows*20>autotuneLog.runningRows)
            ImGui::TextColored(amber,"Logged VE differs from the loaded VE table in %d of %d running rows: the log was probably recorded with a different calibration.",autotuneLog.veMismatchRows,autotuneLog.runningRows);
    }
    ImGui::Spacing();
    if(narrowband) ImGui::TextWrapped("Narrowband mode learns from the ECU's closed-loop trim, averaged over complete lean/rich switching cycles. It can only tune cells whose target AFR is inside the narrowband window (about stoichiometric). Power-enrichment and other rich cells still need a wideband.");
    else ImGui::TextWrapped("Wideband mode compares measured with target AFR one lambda delay later. With closed-loop trim on, the trim is folded into the correction. Confirm the lambda delay for your exhaust.");
    ImGui::TextWrapped("Applying edits only the local VE map. Write active tune or Live tuning sends it. Saving to ECU flash remains a separate action.");
    if(!ImGui::IsAnyItemActive()) saveAutotuneSettings();
}
