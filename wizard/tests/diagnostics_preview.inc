// Compiled only into the isolated visual-test target. Never shipped.
#include <cstdio>
#include <cstring>
#include "cal_navigation.h"
ImVec2 diagnosticPreviewCardCenter;
ImVec2 graphPreviewStart,graphPreviewSize;
bool graphPreviewControls=false;
float graphPreviewInitialSpan=0;
static bool PreviewOption(const char* option) {
    const std::string command=std::string(" ")+GetCommandLineA()+" ";
    return command.find(std::string(" ")+option+" ")!=std::string::npos;
}
static int PreviewIndex(const char* option) {
    const char* value=strstr(GetCommandLineA(),option);
    return value?atoi(value+strlen(option)):-1;
}
static void InitScalingPreview() {
    if(!PreviewOption("--audit-ui")) return;
    if(!gApp.cal.loadFromFile("../../../build/oem/example-schema5.bin"))
        gApp.cal.loadFromFile("../../../build/oem/example-schema4.bin");
    for(auto& tab:gApp.tabs) tab={};
    gApp.numTabs=0; gApp.activeTab=-1;
    const int table=PreviewIndex("--table=");
    const int settings=PreviewIndex("--settings=");
    const int page=PreviewIndex("--page=");
    if(PreviewOption("--search-ect")) snprintf(gApp.search,sizeof(gApp.search),"ECT");
    if(PreviewOption("--search-dtc")) snprintf(gApp.search,sizeof(gApp.search),"DTC");
    if(PreviewOption("--search-ve")) snprintf(gApp.search,sizeof(gApp.search),"Running VE");
    if(page>=0 && page<NUM_CALIBRATION_PAGES) gApp.openScalars(CALIBRATION_PAGES[page].id);
    else if(table>=0 && table<NUM_TABLES) gApp.openTable(table);
    else if(settings>=0) {
        std::set<std::string> categories;
        for(int i=0;i<NUM_SCALARS;++i) categories.insert(ALL_SCALARS[i].category);
        for(int i=0;i<NUM_FLAGS;++i) categories.insert(ALL_FLAGS[i].category);
        for(int i=0;i<NUM_DROPDOWNS;++i) categories.insert(ALL_DROPDOWNS[i].category);
        int index=0;
        for(const auto& category:categories) if(index++==settings) gApp.openScalars(category.c_str());
    } else if(PreviewOption("--logging") || PreviewOption("--csv")) {
        gApp.logState.clear();
        for(int i=0;i<3000;++i) {
            const float t=i*.05f;
            MonitorData m;
            m.valid=true; m.rpm=uint16_t(2800+1400*std::sin(t*.25f));
            m.kpa=uint16_t(70+28*std::sin(t*.25f+.6f)); m.tps=40+25*std::sin(t*.25f+.4f);
            m.measuredAfr=14.2f+.65f*std::sin(t*.6f); m.targetAfr=14.4f; m.trimPercent=3*std::sin(t*.2f);
            m.pulseUs=uint16_t(4100+600*std::sin(t*.2f)); m.ve=82; m.advance=25;
            m.clt=88; m.iat=31; m.battery=13.9f; m.warmup=m.afterstart=m.accel=100;
            m.state=1|4|16|64;
            if(i==2800) m.rpm=6200; // a brief spike must survive zoomed-out rendering
            if(i>2700 && i<2710) m.measuredAfr=0;
            gApp.logState.pushSample(m,i*.05+(i>=2000?2.0:0.0));
        }
        gApp.logState.refreshViewer();
        graphPreviewControls=PreviewOption("--controls");
        if(graphPreviewControls) {
            auto& signal=gApp.logState.viewer.signals[SIG_RPM];
            signal.scaleMode=2; signal.yMin=1000; signal.yMax=7000;
        }
        if(PreviewOption("--csv")) {
            gApp.logViewers.push_back(gApp.logState.viewer);
            auto& view=gApp.logViewers.back(); view.liveMode=false; view.followLatest=false;
            view.viewStart=0; view.viewEnd=view.log.duration; view.tabLabel="Synthetic CSV preview";
            view.cursorTime=0;
            if(PreviewOption("--pan")) { view.viewStart=30; view.viewEnd=90; view.dragToZoom=false; }
            const int index=gApp.allocateTab();
            gApp.tabs[index].open=true; gApp.tabs[index].type=App::OpenTab::LOG_VIEWER; gApp.tabs[index].logViewerIndex=0; gApp.activeTab=index;
            if(PreviewOption("--play")) view.playing=true;
        } else gApp.openLoggingTab();
        const auto& graphView=PreviewOption("--csv")?gApp.logViewers.back():gApp.logState.viewer;
        graphPreviewInitialSpan=graphView.viewEnd-graphView.viewStart;
    } else if(PreviewOption("--gauge")) {
        const int rev=PreviewIndex("--rev=");
        if(rev>=1500 && rev<=10000) { gApp.cal.data[0x5DB]=rev>>8; gApp.cal.data[0x5DC]=rev&255; }
        gApp.openDashboardTab();
    } else gApp.openTable(0);
    if(PreviewOption("--paste-grid")) gApp.tabs[0].editorState.openPasteGrid=true;
    if(PreviewOption("--about")) gApp.showAbout=true;
    if(PreviewOption("--help")) gApp.help.open();
    snprintf(gApp.statusLine,sizeof(gApp.statusLine),"Scaling audit / example calibration / no ECU connected");
}
static void ScalingPreviewMenus() {
    if(!PreviewOption("--audit-ui")) return;
    const char* menu=PreviewOption("--file-menu")?"File":PreviewOption("--ecu-menu")?"ECU":PreviewOption("--view-menu")?"View":nullptr;
    if(menu && ImGui::BeginMainMenuBar()) { ImGui::OpenPopup(menu); ImGui::EndMainMenuBar(); }
}
// The dashboard fixture renders the real panel without opening a serial port.
static void DrawDashboardPreview() {
    static LogState history;
    static bool initialized=false;
    if(!initialized) {
        history.init();
        for(int i=0;i<200;++i) {
            MonitorData sample;
            sample.rpm=uint16_t(2900+300*std::sin(i*.06f));
            sample.kpa=uint16_t(65+7*std::sin(i*.09f));
            history.pushSample(sample,i*.05);
        }
        initialized=true;
    }
    MonitorData sample;
    sample.valid=true; sample.rpm=3200; sample.kpa=72; sample.tps=34;
    sample.measuredAfr=14.4f; sample.targetAfr=14.7f; sample.trimPercent=2.3f;
    sample.clt=88; sample.iat=31; sample.battery=13.9f; sample.speed=68; sample.gear=3;
    sample.ve=82; sample.pulseUs=4200; sample.advance=28.5f;
    sample.warmup=sample.afterstart=sample.accel=100; sample.iacPosition=42; sample.idleTarget=900;
    sample.state=1|4|16|64; sample.flags=16;
    const bool narrowband=strstr(GetCommandLineA(),"--narrowband")!=nullptr;
    if(narrowband) sample.measuredAfr=0;
    if(strstr(GetCommandLineA(),"--inhibits")) { sample.inhibits=0x14; sample.lossOfSyncCount=65535; sample.flags|=6; }
    const bool stale=strstr(GetCommandLineA(),"--stale")!=nullptr;
    auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("Dashboard visual fixture",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Dashboard preview / synthetic readings / no ECU connection");
    static DashboardLayout layout=DashboardLayout::Standard;
    if(strstr(GetCommandLineA(),"--compact")) layout=DashboardLayout::Compact;
    if(strstr(GetCommandLineA(),"--diagnostic")) layout=DashboardLayout::Diagnostic;
    DrawDashboard(sample,true,true,stale?2.0f:.04f,layout,narrowband,&history,7500);
    ImGui::End();
}
static void DiagnosticPreviewInput(int frame) {
    if(PreviewOption("--logging") || PreviewOption("--csv")) {
        auto& io=ImGui::GetIO();
        const float x=graphPreviewStart.x,y=graphPreviewStart.y+graphPreviewSize.y*.5f;
        if(PreviewOption("--zoom") && frame==3) { io.AddMousePosEvent(x+graphPreviewSize.x*.5f,y); io.AddMouseWheelEvent(0,2); }
        if(PreviewOption("--select")) {
            if(frame==2) { io.AddMousePosEvent(x+graphPreviewSize.x*.25f,y); io.AddKeyEvent(ImGuiMod_Shift,true); io.AddMouseButtonEvent(0,true); }
            if(frame==3) io.AddMousePosEvent(x+graphPreviewSize.x*.7f,y);
            if(frame==4) { io.AddMouseButtonEvent(0,false); io.AddKeyEvent(ImGuiMod_Shift,false); }
        }
        if(PreviewOption("--pan")) {
            if(frame==2) { io.AddMousePosEvent(x+graphPreviewSize.x*.5f,y); io.AddMouseButtonEvent(0,true); }
            if(frame==3) io.AddMousePosEvent(x+graphPreviewSize.x*.7f,y);
            if(frame==4) io.AddMouseButtonEvent(0,false);
        }
        if(PreviewOption("--drag-zoom") || PreviewOption("--drag-click")) {
            const bool reverse=PreviewOption("--reverse");
            if(frame==2) { io.AddMousePosEvent(x+graphPreviewSize.x*(reverse?.7f:.25f),y); io.AddMouseButtonEvent(0,true); }
            if(frame==3 && !PreviewOption("--drag-click")) io.AddMousePosEvent(x+graphPreviewSize.x*(reverse?.25f:.7f),y);
            if(frame==4) io.AddMouseButtonEvent(0,false);
        }
    }
    if(!strstr(GetCommandLineA(),"--freeze")) return;
    auto& io=ImGui::GetIO();
    if(frame==3) { io.AddMousePosEvent(diagnosticPreviewCardCenter.x,diagnosticPreviewCardCenter.y); io.AddMouseButtonEvent(0,true); }
    if(frame==4) io.AddMouseButtonEvent(0,false);
}
static void InitDiagnosticPreview() {
    auto& s=gApp.diagnostics.snapshot;
    s.valid=true; s.mil=true; s.capturedAt=GetTickCount64();
    unsigned char raw[24]={}; raw[0]=0x43; raw[2]=1; raw[3]=0x22; raw[4]=8;
    raw[9]=128; raw[10]=74; raw[11]=46; raw[12]=36; raw[13]=141;
    raw[14]=94; raw[17]=62; raw[18]=2; raw[19]=131; raw[20]=3; raw[21]=14; raw[22]=3;
    DtcRecord r; DecodeDtc(raw,24,r); s.records.push_back(r);
    raw[0]=0x61; raw[2]=0; raw[3]=0x22; raw[4]=0; raw[22]=1;
    DecodeDtc(raw,24,r); s.records.push_back(r);
    raw[0]=0x27; raw[2]=1; raw[3]=0x11; raw[4]=8; raw[22]=5;
    DecodeDtc(raw,24,r); s.records.push_back(r);
    gApp.diagnostics.message="Visual test fixture / synthetic records / no ECU connected";
    gApp.openToolTab(App::OpenTab::DIAGNOSTICS);
    if(strstr(GetCommandLineA(),"--empty")) { s.records.clear(); s.mil=false; }
    if(strstr(GetCommandLineA(),"--contrast")) { gApp.uiTheme=UiTheme::HighContrast; ApplyUiTheme(gApp.uiTheme,g_dpiScale); }
}
static bool SaveDiagnosticPreview() {
    if(PreviewOption("--drag-zoom") || PreviewOption("--drag-click")) {
        const auto& view=PreviewOption("--csv")?gApp.logViewers.back():gApp.logState.viewer;
        const float expected=graphPreviewInitialSpan*(PreviewOption("--drag-click")?1:.45f);
        if(std::abs((view.viewEnd-view.viewStart)-expected)>.15f || view.selecting) return false;
    }
    if(PreviewOption("--play") && (gApp.logViewers.back().cursorTime<=0 || !gApp.logViewers.back().playing)) return false;
    if(PreviewOption("--pan") && gApp.logViewers.back().viewStart>=30) return false;
    if(PreviewOption("--zoom")) {
        const auto& view=PreviewOption("--csv")?gApp.logViewers.back():gApp.logState.viewer;
        if(view.viewEnd-view.viewStart>=view.log.duration) return false;
    }
    if(PreviewOption("--select")) {
        const auto& view=PreviewOption("--csv")?gApp.logViewers.back():gApp.logState.viewer;
        if(!view.hasSelection || std::abs(view.selectionEnd-view.selectionStart)<.01f) return false;
    }
    if(strstr(GetCommandLineA(),"--freeze") && gApp.selectedDtc.event!=0x43) return false;
    ID3D11Texture2D *buffer=nullptr,*staging=nullptr;
    if(FAILED(g_pSwapChain->GetBuffer(0,IID_PPV_ARGS(&buffer)))) return false;
    D3D11_TEXTURE2D_DESC desc; buffer->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    if(FAILED(g_pd3dDevice->CreateTexture2D(&desc,nullptr,&staging))) { buffer->Release(); return false; }
    g_pd3dDeviceContext->CopyResource(staging,buffer); buffer->Release();
    D3D11_MAPPED_SUBRESOURCE data;
    if(FAILED(g_pd3dDeviceContext->Map(staging,0,D3D11_MAP_READ,0,&data))) { staging->Release(); return false; }
    const char* name=strstr(GetCommandLineA(),"--dashboard")?"dashboard.bmp":strstr(GetCommandLineA(),"--freeze")?"freeze-frame.bmp":strstr(GetCommandLineA(),"--empty")?"empty-diagnostics.bmp":"diagnostics.bmp";
    FILE* f=fopen(name,"wb");
    if(f) {
        BITMAPFILEHEADER file={}; BITMAPINFOHEADER info={};
        file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(info); file.bfSize=file.bfOffBits+desc.Width*desc.Height*4;
        info.biSize=sizeof(info); info.biWidth=desc.Width; info.biHeight=-LONG(desc.Height); info.biPlanes=1; info.biBitCount=32;
        fwrite(&file,sizeof(file),1,f); fwrite(&info,sizeof(info),1,f);
        std::vector<unsigned char> line(desc.Width*4);
        for(UINT y=0;y<desc.Height;++y) {
            const auto* row=(const unsigned char*)data.pData+y*data.RowPitch;
            for(UINT x=0;x<desc.Width;++x) { line[x*4]=row[x*4+2]; line[x*4+1]=row[x*4+1]; line[x*4+2]=row[x*4]; line[x*4+3]=255; }
            fwrite(line.data(),1,line.size(),f);
        }
        fclose(f);
    }
    g_pd3dDeviceContext->Unmap(staging,0); staging->Release(); return f!=nullptr;
}
