#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <commdlg.h>
#include <io.h>
App gApp;
#include "app_reused.inc"
void App::init() {
    cal.clear(); ecu.setPort(&serial); ecu.initThread(); logState.init();
    comPorts=SerialPort::enumerate(); loadRecentFiles(); openToolTab(OpenTab::AUTOTUNE); openToolTab(OpenTab::DIAGNOSTICS); openDashboardTab();
}
void App::guardedClose() {
    if(ecuBusy()) { snprintf(statusLine,sizeof(statusLine),"Wait for the ECU operation to finish"); return; }
    pendingAction=PendingAction::Close;
    if(!cal.dirty) performPending();
}
void App::guardedOpen() { pendingAction=PendingAction::Open; if(!cal.dirty) performPending(); }
void App::performPending() {
    auto p=pendingAction; pendingAction=PendingAction::None;
    if(p==PendingAction::Close) DestroyWindow(hwnd);
    if(p==PendingAction::Open) fileOpen();
    if(p==PendingAction::Read) ecuRead();
}
void App::fileOpen() {
    char path[260]={};
    if(!FileDialog(path,sizeof(path),false)) return;
    if(!cal.loadFromFile(path)) { snprintf(statusLine,sizeof(statusLine),"Expected a 3072-byte schema-4 calibration"); return; }
    undo.clear(); ecuSynced=false; liveTuning=false; addRecentFile(path);
    snprintf(statusLine,sizeof(statusLine),"Opened %s",path);
}
void App::fileCompare() {
    char path[260]={}; CalBuffer other;
    if(!FileDialog(path,sizeof(path),false)) return;
    if(!other.loadFromFile(path)) { snprintf(statusLine,sizeof(statusLine),"Compare requires a schema-4 calibration"); return; }
    memcpy(compareData,other.data,CAL_SIZE); compareActive=true;
}
void App::connect() {
    if(selectedPort<0 || selectedPort>=int(comPorts.size())) return;
    if(!serial.open(comPorts[selectedPort].c_str())) { snprintf(statusLine,sizeof(statusLine),"Could not open serial port"); return; }
    Capabilities caps; MonitorData m;
    if(!ecu.capabilities(caps)) {
        serial.close(); snprintf(statusLine,sizeof(statusLine),"Unsupported calibration protocol"); return;
    }
    diagnostics={};
    updateOnly=!ecu.readMonitor(m);
    ecuSynced=false; liveTuning=false; ecu.clearMonitorUpdates(); ecu.resetMonitorFailCount();
    if(updateOnly) {
        snprintf(statusLine,sizeof(statusLine),"Connected for tune backup and firmware update; compatible live frame required for tuning");
    } else {
        ecu.monitor=m; monitorLastUpdateTick=GetTickCount(); if(!updateOnly) ecu.startMonitor();
        snprintf(statusLine,sizeof(statusLine),"Connected to %s",serial.portName.c_str());
    }
}
void App::disconnect() {
    if(!ecu.stopMonitor()) { snprintf(statusLine,sizeof(statusLine),"Monitor is still stopping"); return; }
    serial.close(); ecu.clearMonitorUpdates(); ecuSynced=false; liveTuning=false;
    ecu.monitor.valid=false; autotune.clear(); autotune.collecting=false;
    snprintf(statusLine,sizeof(statusLine),"Disconnected");
}
bool App::beginOperation(Operation op) {
    if(ecuBusy() || !serial.isOpen()) return false;
    if(updateOnly && op!=Operation::Read) {
        snprintf(statusLine,sizeof(statusLine),"Update firmware before tuning with this live-frame version"); return false;
    }
    if(op!=Operation::Save && !CreateCalibrationBackup(cal,op==Operation::Read?"before-read":"before-write")) {
        snprintf(statusLine,sizeof(statusLine),"Could not create calibration backup"); return false;
    }
    autotune.clear();
    if(!ecu.stopMonitor()) return false;
    ecu.clearMonitorUpdates();
    bool ok=false;
    if(op==Operation::Read || op==Operation::Compare) ok=transfer.read(ecu,op==Operation::Compare);
    if(op==Operation::Write) {
        Capabilities c;
        if(!ecu.capabilities(c) || c.generation!=baselineGeneration) ecuSynced=false;
        ok=transfer.write(ecu,cal,ecuData,ecuSynced);
    }
    if(op==Operation::Save) ok=transfer.save(ecu);
    snprintf(statusLine,sizeof(statusLine),"%s",transfer.message.c_str());
    if(ok) { operation=op; transferState=TransferState::Active; }
    else if(!updateOnly) ecu.startMonitor();
    return ok;
}
void App::ecuRead() { beginOperation(Operation::Read); }
bool App::ecuWrite() { return beginOperation(ecuSynced?Operation::Write:Operation::Compare); }
void App::ecuSave() { liveTuning=false; beginOperation(Operation::Save); }
void App::firmwareFlashSelect() {
    char path[260]={}; std::string error;
    if(!FileDialog(path,sizeof(path),false)) return;
    if(!LoadAndValidateFirmwareImage(path,pendingFirmwareImage,pendingFirmwareInfo,error)) {
        snprintf(statusLine,sizeof(statusLine),"%s",error.c_str()); return;
    }
    backupBeforeFlash=true; confirmFirmwareFlash=true;
}
void App::beginFlash() {
    if(!ecu.stopMonitor()) return;
    Capabilities c; TpsSnapshot snapshot;
    // Snapshot stopped-state is independent of compact-frame revision. This
    // permits upgrading a previous standalone image without misdecoding v1.
    if(!ecu.capabilities(c) || !ecu.tpsSnapshot(snapshot) || !(snapshot.flags&2)) {
        snprintf(statusLine,sizeof(statusLine),"Flashing requires a fresh stopped-engine report");
        if(!updateOnly) ecu.startMonitor(); return;
    }
    liveTuning=false; ecuSynced=false; ecu.clearMonitorUpdates();
    flashCompletionHandled=!firmwareFlasher.start(&ecu,pendingFirmwareImage,pendingFirmwareInfo);
}
void App::update(float dt) {
    elapsed+=dt;
    if(logState.recording) logState.recordingElapsed+=dt;
    if(firmwareFlasher.busy()) { snprintf(statusLine,sizeof(statusLine),"%s",firmwareFlasher.status().c_str()); return; }
    if(!flashCompletionHandled) {
        firmwareFlasher.joinIfFinished(); flashCompletionHandled=true;
        disconnect(); snprintf(statusLine,sizeof(statusLine),"%s Reconnect to continue.",firmwareFlasher.status().c_str());
    }
    if(diagnostics.busy()) {
        diagnostics.step(ecu,GetTickCount64());
        snprintf(statusLine,sizeof(statusLine),"%s",diagnostics.message.c_str());
        if(!diagnostics.busy() && serial.isOpen() && !updateOnly) ecu.startMonitor();
        return;
    }
    if(transfer.busy()) {
        transfer.step(ecu);
        snprintf(statusLine,sizeof(statusLine),"%s",transfer.message.c_str());
        if(!transfer.busy()) {
            transferState=TransferState::Idle;
            const bool success=transfer.phase==TuneTransfer::Phase::Complete;
            if(success && operation!=Operation::Save) {
                memcpy(ecuData,transfer.image.data(),CAL_SIZE); ecuSynced=true;
                baselineGeneration=transfer.verifiedGeneration();
                if(operation==Operation::Read && !flashBackupPending) {
                    memcpy(cal.data,ecuData,CAL_SIZE); cal.loaded=true; cal.markClean(); cal.filePath[0]=0; undo.clear();
                }
            } else if(!success) { ecuSynced=false; liveTuning=false; }
            if(flashBackupPending) {
                flashBackupPending=false;
                CalBuffer backup; memcpy(backup.data,transfer.image.data(),CAL_SIZE); backup.loaded=success;
                if(success && backup.saveToFile(backupPath)) { beginFlash(); return; }
                snprintf(statusLine,sizeof(statusLine),"ECU tune backup failed; firmware update cancelled");
            }
            if(success && operation==Operation::Compare) { beginOperation(Operation::Write); return; }
            if(!updateOnly) ecu.startMonitor();
        }
        return;
    }
    if(cal.loaded && (!ecuSynced || memcmp(cal.data,ecuData,CAL_SIZE))) autotune.clear();
    MonitorData m;
    if(ecu.consumeMonitorUpdate(m)) {
        ecu.monitor=m; monitorLastUpdateTick=GetTickCount(); logState.pushSample(m);
        autotune.sample(m,elapsed,cal.loaded && ecuSynced && !memcmp(cal.data,ecuData,CAL_SIZE) && cal.data[0x600]==1);
        if(ecuSynced && baselineGeneration!=m.generation) { ecuSynced=false; liveTuning=false; }
    }
    if(serial.isOpen() && ecu.getMonitorFailCount()>=10) {
        disconnect(); snprintf(statusLine,sizeof(statusLine),"Connection lost or unsupported live frame; reconnect and read ECU");
    }
    if(liveTuning && ecuSynced && cal.loaded && serial.isOpen()) {
        auto changes=BuildDirtyRanges(cal.data,ecuData,true);
        if(!changes.empty()) {
            if(changes.back().end<=0x400) { if(!ecuWrite()) liveTuning=false; }
            else { liveTuning=false; snprintf(statusLine,sizeof(statusLine),"Live tuning paused: use Write active tune for settings or axes"); }
        }
    }
}
void App::drawUI() {
    const bool busy=ecuBusy();
    if(ImGui::BeginMainMenuBar()) {
        ImGui::BeginDisabled(busy);
        if(ImGui::BeginMenu("File")) {
            if(ImGui::MenuItem("Open tune...","Ctrl+O")) guardedOpen();
            if(ImGui::MenuItem("Save file","Ctrl+S",false,cal.loaded)) fileSave();
            if(ImGui::MenuItem("Save file as...",nullptr,false,cal.loaded)) fileSaveAs();
            if(ImGui::MenuItem("Compare tune...",nullptr,false,cal.loaded)) fileCompare();
            if(ImGui::MenuItem("Clear comparison",nullptr,false,compareActive)) compareActive=false;
            if(ImGui::MenuItem("Open CSV log...")) openLogViewer();
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("ECU")) {
            if(ImGui::MenuItem("Read calibration",nullptr,false,serial.isOpen())) { pendingAction=PendingAction::Read; if(!cal.dirty) performPending(); }
            if(ImGui::MenuItem("Write active tune","Ctrl+W",false,serial.isOpen()&&cal.loaded&&!updateOnly)) ecuWrite();
            if(ImGui::MenuItem("Save tune to ECU flash...",nullptr,false,serial.isOpen()&&ecu.monitor.valid&&ecu.monitor.rpm==0)) confirmSave=true;
            if(ImGui::MenuItem("Update firmware...",nullptr,false,serial.isOpen())) firmwareFlashSelect();
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("View")) {
            if(ImGui::MenuItem("Dashboard")) openDashboardTab();
            if(ImGui::MenuItem("Live logging")) openLoggingTab();
            if(ImGui::MenuItem("Diagnostics")) openToolTab(OpenTab::DIAGNOSTICS);
            if(ImGui::MenuItem("Autotune")) openToolTab(OpenTab::AUTOTUNE);
            if(ImGui::MenuItem("Review calibration",nullptr,false,cal.loaded)) showReviewIssues=true;
            const char* names[]={"Workshop","High contrast","Color vision safe"};
            for(int i=0;i<3;++i) if(ImGui::MenuItem(names[i])) { uiTheme=UiTheme(i); ApplyUiTheme(uiTheme,S(1)); }
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Help")) help.open();
        if(ImGui::MenuItem("About")) showAbout=true;
        ImGui::EndDisabled(); ImGui::EndMainMenuBar();
    }
    const auto* vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Tuning Wizard",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    ImGui::BeginDisabled(busy);
    if(ImGui::Button("Refresh ports")) { comPorts=SerialPort::enumerate(); selectedPort=0; }
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(130));
    const char* selected=selectedPort<int(comPorts.size())?comPorts[selectedPort].c_str():"No ports";
    if(ImGui::BeginCombo("##Port",selected)) {
        for(int i=0;i<int(comPorts.size());++i) if(ImGui::Selectable(comPorts[i].c_str(),i==selectedPort)) selectedPort=i;
        ImGui::EndCombo();
    }
    ImGui::SameLine(); if(ImGui::Button(serial.isOpen()?"Disconnect":"Connect")) { if(serial.isOpen()) disconnect(); else connect(); }
    ImGui::SameLine(); ImGui::BeginDisabled(!ecuSynced||!serial.isOpen()||updateOnly); ImGui::Checkbox("Live tuning",&liveTuning); ImGui::EndDisabled();
    if(updateOnly) { ImGui::SameLine(); ImGui::TextUnformatted("Firmware update required for live tuning"); }
    if(ecu.monitor.valid && ecu.monitor.unsaved()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,.8f,.2f,1),"Unsaved ECU tune"); }
    ImGui::EndDisabled();
    ImGui::Separator();
    const float height=ImGui::GetContentRegionAvail().y-S(65);
    ImGui::BeginChild("Navigation",ImVec2(S(235),height),ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Tuning Wizard");
    if(ImGui::Selectable("Dashboard")) openDashboardTab();
    if(ImGui::Selectable("Diagnostics")) openToolTab(OpenTab::DIAGNOSTICS);
    if(ImGui::Selectable("Autotune")) openToolTab(OpenTab::AUTOTUNE);
    if(ImGui::Selectable("Live logging")) openLoggingTab();
    ImGui::SeparatorText("Calibration"); ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##Search","Search calibration...",search,sizeof(search));
    ImGui::BeginDisabled(!cal.loaded||busy);
    std::set<std::string> categories;
    for(int i=0;i<NUM_TABLES;++i) categories.insert(ALL_TABLES[i].category);
    for(int i=0;i<NUM_SCALARS;++i) categories.insert(ALL_SCALARS[i].category);
    for(int i=0;i<NUM_FLAGS;++i) categories.insert(ALL_FLAGS[i].category);
    auto matches=[&](const char* name) {
        if(!search[0]) return true;
        std::string a=name,b=search;
        std::transform(a.begin(),a.end(),a.begin(),[](unsigned char c){return char(tolower(c));});
        std::transform(b.begin(),b.end(),b.begin(),[](unsigned char c){return char(tolower(c));});
        return a.find(b)!=std::string::npos;
    };
    for(const auto& category:categories) {
        if(search[0]) ImGui::SetNextItemOpen(true);
        if(ImGui::TreeNode(category.c_str())) {
            for(int i=0;i<NUM_TABLES;++i) if(category==ALL_TABLES[i].category && matches(ALL_TABLES[i].name))
                if(ImGui::Selectable(ALL_TABLES[i].name)) openTable(i);
            bool has=false;
            for(int i=0;i<NUM_SCALARS;++i) has|=category==ALL_SCALARS[i].category && matches(ALL_SCALARS[i].name);
            for(int i=0;i<NUM_FLAGS;++i) has|=category==ALL_FLAGS[i].category && matches(ALL_FLAGS[i].name);
            for(int i=0;i<NUM_DROPDOWNS;++i) has|=category==ALL_DROPDOWNS[i].category && matches(ALL_DROPDOWNS[i].name);
            if(has && ImGui::Selectable("Settings")) openScalars(category.c_str());
            ImGui::TreePop();
        }
    }
    ImGui::EndDisabled(); ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("Editors",ImVec2(0,height));
    const float age=monitorLastUpdateTick?(GetTickCount()-monitorLastUpdateTick)*.001f:999;
    MonitorData fresh=ecu.monitor; if(age>=1 || busy) fresh.valid=false;
    if(ImGui::BeginTabBar("Tabs",ImGuiTabBarFlags_Reorderable)) {
        for(int i=0;i<numTabs;++i) {
            auto& t=tabs[i]; if(!t.open) continue;
            std::string label=t.type==OpenTab::TABLE?ALL_TABLES[t.tableIndex].name:
                t.type==OpenTab::SCALARS||t.type==OpenTab::FLAGS?t.categoryFilter:
                t.type==OpenTab::DASHBOARD?"Dashboard":t.type==OpenTab::LOGGING?"Logging":
                t.type==OpenTab::DIAGNOSTICS?"Diagnostics":t.type==OpenTab::AUTOTUNE?"Autotune":logViewers[t.logViewerIndex].tabLabel;
            label+="###tab"+std::to_string(i);
            if(ImGui::BeginTabItem(label.c_str(),&t.open,activeTab==i?ImGuiTabItemFlags_SetSelected:0)) {
                if(activeTab==i) activeTab=-1;
                ImGui::BeginDisabled(busy);
                if(t.type==OpenTab::TABLE && cal.loaded) DrawTableEditor(ALL_TABLES[t.tableIndex],cal,undo,t.editorState,&fresh,compareActive?compareData:nullptr);
                if((t.type==OpenTab::SCALARS||t.type==OpenTab::FLAGS) && cal.loaded) {
                    DrawScalarEditors(t.categoryFilter,cal,undo,serial.isOpen()?&ecu:nullptr);
                    DrawDropdownEditors(t.categoryFilter,cal,undo); DrawFlagEditors(t.categoryFilter,cal,undo);
                }
                ImGui::EndDisabled();
                if(t.type==OpenTab::DASHBOARD) DrawDashboard(fresh,serial.isOpen(),ecu.monitorActive,age,dashboardLayout,false);
                if(t.type==OpenTab::LOGGING) DrawLoggingTab(logState,fresh,serial.isOpen(),ecu.monitorActive,age);
                if(t.type==OpenTab::LOG_VIEWER) DrawLogViewer(logViewers[t.logViewerIndex]);
                if(t.type==OpenTab::DIAGNOSTICS) drawDiagnostics();
                if(t.type==OpenTab::AUTOTUNE) drawAutotune();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild(); ImGui::Separator();
    ImGui::TextWrapped("%s",statusLine);
    if(busy) ImGui::ProgressBar(firmwareFlasher.busy()?firmwareFlasher.progress():diagnostics.busy()?diagnostics.progress():transfer.progress());
    else DrawInlineDashboard(fresh,serial.isOpen(),ecu.monitorActive,age,false);
    ImGui::End();
    if(pendingAction!=PendingAction::None) ImGui::OpenPopup("Unsaved file changes");
    if(ImGui::BeginPopupModal("Unsaved file changes",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save local changes before continuing?");
        if(ImGui::Button("Save file")) { fileSave(); if(!cal.dirty) { performPending(); ImGui::CloseCurrentPopup(); } }
        ImGui::SameLine(); if(ImGui::Button("Discard")) { performPending(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if(ImGui::Button("Cancel")) { pendingAction=PendingAction::None; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if(confirmSave) { ImGui::OpenPopup("Save active tune"); confirmSave=false; }
    if(ImGui::BeginPopupModal("Save active tune",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save the ECU's active tune to flash? Engine must be stopped.");
        ImGui::TextUnformatted("Outputs remain off until a key cycle after saving.");
        ImGui::TextUnformatted("Local edits must be written to the active tune first.");
        bool differs=cal.loaded && (!ecuSynced || memcmp(cal.data,ecuData,CAL_SIZE));
        ImGui::BeginDisabled(differs);
        if(ImGui::Button("Save tune to ECU flash")) { ecuSave(); ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled();
        if(differs) ImGui::TextUnformatted("Read or write the active tune before saving.");
        ImGui::SameLine(); if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(confirmFirmwareFlash) { ImGui::OpenPopup("Update firmware"); confirmFirmwareFlash=false; }
    if(ImGui::BeginPopupModal("Update firmware",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Image: %zu bytes; %d flash sectors will be erased",pendingFirmwareInfo.size,pendingFirmwareInfo.sectorsToErase);
        ImGui::TextUnformatted("Both stored tunes will be erased. Restore a saved tune if this image contains no calibration.");
        ImGui::Checkbox("Read and save the current ECU tune first",&backupBeforeFlash);
        if(ImGui::Button("Program firmware")) {
            if(backupBeforeFlash) {
                backupPath[0]=0;
                if(FileDialog(backupPath,sizeof(backupPath),true) && beginOperation(Operation::Read)) flashBackupPending=true;
            } else beginFlash();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
    }
    if(showReviewIssues) {
        if(ImGui::Begin("Calibration review",&showReviewIssues)) {
            auto issues=ValidateCalibration(cal);
            if(issues.empty()) ImGui::TextUnformatted("All firmware validation rules pass.");
            for(const auto& issue:issues) { ImGui::TextWrapped("%s",issue.message.c_str()); if(!issue.allowed.empty()) ImGui::TextWrapped("%s",issue.allowed.c_str()); }
            ImGui::TextUnformatted("Structural ranges require a stopped engine:");
            for(int i=0;i<NUM_STRUCTURAL_RANGES;++i) ImGui::Text("0x%03X - 0x%03X",STRUCTURAL_RANGES[i].start,STRUCTURAL_RANGES[i].end-1);
        }
        ImGui::End();
    }
    if(showAbout) {
        if(ImGui::Begin("About Tuning Wizard",&showAbout)) { ImGui::TextUnformatted("Tuning Wizard"); ImGui::Text("Version %s",TW_VERSION); ImGui::TextUnformatted("Standalone ECU calibration and logging"); }
        ImGui::End();
    }
    DrawHelpBook(help);
    if(!busy && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
        if(ImGui::IsKeyPressed(ImGuiKey_O,false)) guardedOpen();
        if(ImGui::IsKeyPressed(ImGuiKey_S,false) && cal.loaded) fileSave();
        if(ImGui::IsKeyPressed(ImGuiKey_W,false) && serial.isOpen() && cal.loaded) ecuWrite();
    }
    std::string title=cal.filePath[0]?cal.filePath:"Tuning Wizard";
    if(cal.filePath[0]) title+=" - Tuning Wizard";
    if(cal.dirty) title+=" *";
    SetWindowTextA(hwnd,title.c_str());
}
