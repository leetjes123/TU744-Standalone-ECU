#include "dashboard.h"
#include "logging.h"
#include "imgui_freetype.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
extern float g_dpiScale;
namespace {
ImFont* instrumentFont = nullptr;
ImFont* largeInstrumentFont = nullptr;
float D(float n) { return n * g_dpiScale; }
ImU32 Muted() { return ImGui::GetColorU32(ImGuiCol_TextDisabled); }
ImU32 Accent() { return ImGui::GetColorU32(ImGuiCol_PlotHistogram); }
ImU32 Trace() { return ImGui::GetColorU32(ImGuiCol_PlotLines); }

void Instrument(const char* value, bool available, bool large = false) {
    ImFont* font = large ? largeInstrumentFont : instrumentFont;
    if(font) ImGui::PushFont(font);
    if(available) ImGui::TextUnformatted(value);
    else ImGui::TextDisabled("--");
    if(font) ImGui::PopFont();
}

void Bar(float value, float low, float high, bool fresh, ImU32 color) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = D(8);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p,ImVec2(p.x+w,p.y+h),ImGui::GetColorU32(ImGuiCol_FrameBg),D(3));
    if(fresh) {
        const float f = std::clamp((value-low)/(high-low),0.0f,1.0f);
        if(f>0) draw->AddRectFilled(p,ImVec2(p.x+w*f,p.y+h),color,D(3));
    }
    ImGui::Dummy(ImVec2(w,h));
}

bool Card(const char* id, float height) {
    return ImGui::BeginChild(id,ImVec2(0,D(height)),ImGuiChildFlags_Borders,
                             ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
}

void Tachometer(const MonitorData& m, bool fresh, int maximumRpm) {
    ImGui::TextDisabled("ENGINE SPEED");
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float r = std::min(w*.43f,D(112));
    const ImVec2 center(p.x+w*.5f,p.y+r+D(8));
    auto* draw = ImGui::GetWindowDrawList();
    const float pi = 3.14159265f;
    draw->PathArcTo(center,r,pi,2*pi,96);
    draw->PathStroke(ImGui::GetColorU32(ImGuiCol_FrameBg),0,D(10));
    if(fresh && m.rpm>0) {
        draw->PathArcTo(center,r,pi,pi+pi*std::clamp(float(m.rpm)/maximumRpm,0.0f,1.0f),96);
        draw->PathStroke(Accent(),0,D(10));
    }
    for(int i=0;i<=8;++i) {
        const float a=pi+pi*i/8;
        draw->AddLine(ImVec2(center.x+std::cos(a)*(r-D(12)),center.y+std::sin(a)*(r-D(12))),
                      ImVec2(center.x+std::cos(a)*(r-D(18)),center.y+std::sin(a)*(r-D(18))),Muted(),D(1));
    }
    char value[24]; snprintf(value,sizeof(value),"%u",m.rpm);
    ImFont* font=largeInstrumentFont?largeInstrumentFont:ImGui::GetFont();
    const char* text=fresh?value:"--";
    const ImVec2 size=font->CalcTextSizeA(font->FontSize,1000,0,text);
    draw->AddText(font,font->FontSize,ImVec2(center.x-size.x*.5f,center.y-D(53)),
                  fresh?ImGui::GetColorU32(ImGuiCol_Text):Muted(),text);
    draw->AddText(ImVec2(center.x-D(13),center.y-D(9)),Muted(),"RPM");
    draw->AddText(ImVec2(center.x-r,center.y+D(9)),Muted(),"0");
    char maximumLabel[24]; snprintf(maximumLabel,sizeof(maximumLabel),"%gk",maximumRpm*.001f);
    draw->AddText(ImVec2(center.x+r-ImGui::CalcTextSize(maximumLabel).x,center.y+D(9)),Muted(),maximumLabel);
    ImGui::Dummy(ImVec2(w,r+D(41)));
    if(fresh) ImGui::Text("Speed %d km/h   |   Gear %d",m.speed,m.gear);
    else ImGui::TextDisabled("Speed --   |   Gear --");
    if(ImGui::IsWindowHovered()) {
        ImGui::BeginTooltip(); ImGui::Text("Display scale: 0-%d RPM. With a loaded tune: rev limit + 1000 RPM.",maximumRpm); ImGui::EndTooltip();
    }
}

void Readout(LogSignal signal, const char* value, bool fresh) {
    const auto& def=SIGNAL_DEFS[signal];
    ImGui::TextDisabled("%s",def.shortName);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s",def.name,def.description);
    Instrument(value,fresh);
}

void Status(const char* label, bool active, bool fresh, bool warning=false) {
    char value[64]; snprintf(value,sizeof(value),"%s: %s",label,!fresh?"--":active?"ON":"OFF");
    const float width=ImGui::CalcTextSize(value).x+ImGui::GetStyle().FramePadding.x*2;
    if(ImGui::GetCursorPosX()>ImGui::GetStyle().WindowPadding.x &&
       ImGui::GetContentRegionAvail().x<width) ImGui::NewLine();
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const float h=ImGui::GetFrameHeight();
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(p,ImVec2(p.x+width,p.y+h),ImGui::GetColorU32(ImGuiCol_FrameBg),D(3));
    if(fresh && active) draw->AddRect(p,ImVec2(p.x+width,p.y+h),warning?Accent():Trace(),D(3),0,D(2));
    draw->AddText(ImVec2(p.x+ImGui::GetStyle().FramePadding.x,p.y+ImGui::GetStyle().FramePadding.y),
                  fresh&&active?ImGui::GetColorU32(ImGuiCol_Text):Muted(),value);
    ImGui::Dummy(ImVec2(width,h)); ImGui::SameLine();
}

void Trend(const LogState* log, LogSignal signal, const char* title, bool fresh) {
    ImGui::TextDisabled("%s",title);
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const float w=ImGui::GetContentRegionAvail().x,h=D(62);
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(p,ImVec2(p.x+w,p.y+h),ImGui::GetColorU32(ImGuiCol_FrameBg),D(3));
    for(int i=1;i<4;++i) draw->AddLine(ImVec2(p.x,p.y+h*i/4),ImVec2(p.x+w,p.y+h*i/4),ImGui::GetColorU32(ImGuiCol_Border));
    if(fresh && log && log->sampleCount>1) {
        const int count=std::min(log->sampleCount,200);
        const auto& def=SIGNAL_DEFS[signal];
        ImVec2 previous;
        for(int i=0;i<count;++i) {
            const int index=(log->writeIdx-count+i+LOG_MAX_SAMPLES)%LOG_MAX_SAMPLES;
            const float f=std::clamp((log->data[signal][index]-def.defaultMin)/(def.defaultMax-def.defaultMin),0.0f,1.0f);
            const ImVec2 point(p.x+w*i/(count-1),p.y+h-D(3)-(h-D(6))*f);
            if(i) draw->AddLine(previous,point,Trace(),D(1.5f));
            previous=point;
        }
    } else draw->AddText(ImVec2(p.x+D(8),p.y+D(20)),Muted(),fresh?"Waiting for samples":"Live history unavailable");
    ImGui::Dummy(ImVec2(w,h));
}
}

void LoadDashboardFonts(float dpiScale) {
    ImFontConfig config;
    config.OversampleH=config.OversampleV=1;
    config.PixelSnapH=true;
    config.FontBuilderFlags=ImGuiFreeTypeBuilderFlags_ForceAutoHint;
    auto* atlas=ImGui::GetIO().Fonts;
    instrumentFont=atlas->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",28*dpiScale,&config);
    largeInstrumentFont=atlas->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",44*dpiScale,&config);
    if(!instrumentFont) { config.SizePixels=28*dpiScale; instrumentFont=atlas->AddFontDefault(&config); }
    if(!largeInstrumentFont) { config.SizePixels=44*dpiScale; largeInstrumentFont=atlas->AddFontDefault(&config); }
}

void DrawDashboard(const MonitorData& m,bool connected,bool monitoring,float age,DashboardLayout& layout,bool narrowband,const LogState* history,int rpmGaugeMax) {
    if(rpmGaugeMax<2500 || rpmGaugeMax>11000) rpmGaugeMax=8000;
    const bool fresh=connected && monitoring && m.valid && age<1.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,D(6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(D(14),D(12)));
    ImGui::BeginChild("Dashboard content",ImVec2(0,0));
    ImGui::TextColored(fresh?ImGui::GetStyleColorVec4(ImGuiCol_PlotLines):ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram),
        "%s",!connected?"DISCONNECTED":!monitoring?"MONITORING OFF":fresh?"LIVE DATA":"DATA STALE / PAUSED");
    ImGui::SameLine();
    if(connected && monitoring && age<999) ImGui::TextDisabled("Sample age %.2f s",age);
    else ImGui::TextDisabled("Sample age --");
    const char* layouts[]={"Compact","Standard","Diagnostic"};
    int selected=int(layout);
    ImGui::SetNextItemWidth(D(150));
    if(ImGui::Combo("Layout",&selected,layouts,3)) layout=DashboardLayout(selected);
    if(fresh) {
        if(m.unsaved()) ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram),"Unsaved tune in ECU RAM");
        if(m.flags&32) ImGui::TextUnformatted("Saving tune - monitoring may pause");
        if(!m.synced()) ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram),"Crank sync not acquired");
        if(m.inhibits) ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram),"Outputs inhibited (0x%04X) - see engine status",m.inhibits);
    }
    const float width=ImGui::GetContentRegionAvail().x;
    const int columns=width>=D(850)?3:width>=D(550)?2:1;
    char value[96];
    if(ImGui::BeginTable("Primary instruments",columns,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if(Card("Engine speed",250)) Tachometer(m,fresh,rpmGaugeMax);
        ImGui::EndChild();
        ImGui::TableNextColumn();
        if(Card("Engine load",250)) {
            ImGui::TextDisabled("ENGINE LOAD");
            snprintf(value,sizeof(value),"%u kPa",m.kpa); Readout(SIG_MAP,value,fresh);
            Bar(float(m.kpa),0,300,fresh,Trace());
            ImGui::Spacing();
            snprintf(value,sizeof(value),"%.1f%%",m.tps); Readout(SIG_TPS,value,fresh);
            Bar(m.tps,0,100,fresh,Accent());
            ImGui::TextDisabled("MAP display scale: 0-300 kPa");
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        if(Card("Mixture",250)) {
            ImGui::TextDisabled("MIXTURE");
            snprintf(value,sizeof(value),"%.1f",m.measuredAfr); Readout(SIG_AFR,value,fresh&&!narrowband&&m.measuredAfr>0);
            snprintf(value,sizeof(value),"%.1f",m.targetAfr); Readout(SIG_TARGET_AFR,value,fresh&&m.targetAfr>0);
            snprintf(value,sizeof(value),"%+.1f%%",m.trimPercent); Readout(SIG_STFT,value,fresh);
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    if(layout!=DashboardLayout::Compact) {
        const int detailColumns=width>=D(720)?2:1;
        if(ImGui::BeginTable("Secondary instruments",detailColumns,ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            if(Card("Temperatures and voltage",220)) {
                ImGui::TextDisabled("TEMPERATURES / ELECTRICAL");
                if(fresh) ImGui::Text("ECT   %d C",m.clt); else ImGui::TextDisabled("ECT   -- C");
                Bar(float(m.clt),-40,150,fresh,Trace()); ImGui::Spacing();
                if(fresh) ImGui::Text("IAT   %d C",m.iat); else ImGui::TextDisabled("IAT   -- C");
                Bar(float(m.iat),-40,150,fresh,Trace()); ImGui::Spacing();
                if(fresh) ImGui::Text("Battery Voltage   %.1f V",m.battery); else ImGui::TextDisabled("Battery Voltage   -- V");
                Bar(m.battery,0,20,fresh,Accent());
                ImGui::TextDisabled("Display ranges: -40..150 C / 0..20 V");
            }
            ImGui::EndChild();
            ImGui::TableNextColumn();
            if(Card("Fuel and ignition",220)) {
                ImGui::TextDisabled("FUEL / IGNITION PLAN");
                const LogSignal fuelSignals[]={SIG_VE,SIG_INJ_PW,SIG_INJ_DUTY,SIG_TIMING};
                for(LogSignal signal:fuelSignals) {
                    const auto& def=SIGNAL_DEFS[signal];
                    if(fresh) ImGui::Text("%s   %.2f %s",def.shortName,LogState::getSignalValue(signal,m),def.unit);
                    else ImGui::TextDisabled("%s   -- %s",def.shortName,def.unit);
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s",def.name,def.description);
                    if(signal==SIG_INJ_DUTY) Bar(m.plannedDutyPercent(),0,100,fresh,Accent());
                }
                if(fresh) {
                    ImGui::Text("WUE %d%%   ASE %d%%   AE %d%%",m.warmup,m.afterstart,m.accel);
                    ImGui::Text("Idle valve %d steps   Target %d RPM",m.iacPosition,m.idleTarget);
                } else {
                    ImGui::TextDisabled("WUE --   ASE --   AE --");
                    ImGui::TextDisabled("Idle valve --   Target -- RPM");
                }
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("ENGINE STATUS");
    if(fresh && m.knockAvailable) {
        if(m.knockMv!=65535) ImGui::Text("Knock Level %.3f V%s",m.knockMv*.001f,(m.knockFlags&2)?"":" (stale)");
        else ImGui::TextDisabled("Knock Level N/A");
        ImGui::SameLine(); ImGui::Text("Knock Retard (requested) %.2f deg",m.knockRetard);
        Status("Knock detected",m.knockFlags&1,true,true);
        Status("Knock Sensor Fault",m.knockFlags&16,true,true);
        Status("Knock control",m.knockFlags&4,true);
        Status("Knock monitor only",m.knockFlags&8,true);
    } else ImGui::TextDisabled("Knock data N/A");
    Status("Trigger Sync",m.synced(),fresh); Status("Running",m.state&4,fresh); Status("Cranking",m.state&2,fresh);
    Status("Closed loop",m.state&16,fresh); Status("Pump",m.state&64,fresh); Status("Fan",m.state&32,fresh);
    Status("Fuel cut",m.flags&2,fresh,true); Status("Spark cut",m.flags&4,fresh,true);
    Status("Overrun cut",m.state&8,fresh); Status("Rev limit",m.flags&1,fresh,true);
    Status("Launch",m.state&128,fresh); Status("Anti-lag",m.flags&8,fresh); Status("MIL",m.flags&64,fresh,true);
    ImGui::NewLine();
    if(fresh) ImGui::Text("Trigger Sync Losses: %u%s",m.lossOfSyncCount,m.lossOfSyncCount==65535?"+":"");
    else ImGui::TextDisabled("Trigger Sync Losses: --");
    if(fresh && m.inhibits) {
        const char* reasons[]={"Crank sync","Calibration","Sensor","Stale input","Service mode","Deadline","Power","Board","Output"};
        ImGui::TextUnformatted("Output inhibits:");
        for(int i=0;i<9;++i) if(m.inhibits&(1u<<i)) ImGui::BulletText("%s",reasons[i]);
        if(m.inhibits&~0x1ffu) ImGui::BulletText("Unknown bits: 0x%04X",m.inhibits&~0x1ffu);
    } else ImGui::TextDisabled("Output inhibits: %s",fresh?"none":"--");
    if(layout!=DashboardLayout::Compact && ImGui::CollapsingHeader("Recent engine trends / up to 200 accepted samples")) {
        Trend(history,SIG_RPM,"RPM / 0-8000",fresh);
        Trend(history,SIG_MAP,"MAP / 0-300 kPa",fresh);
    }
    if(layout==DashboardLayout::Diagnostic) {
    for(int pass=0;pass<2;++pass) {
    if(pass==1 && !ImGui::CollapsingHeader("Advanced / ECU Status")) continue;
    if(ImGui::BeginTable(pass?"ECU internals":"Live signals",3,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Signal"); ImGui::TableSetupColumn("Value"); ImGui::TableSetupColumn("Unit"); ImGui::TableHeadersRow();
        for(int i=0;i<SIG_COUNT;++i) {
            if(SIGNAL_DEFS[i].advanced!=(pass==1)) continue;
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(SIGNAL_DEFS[i].name);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",SIGNAL_DEFS[i].description);
            ImGui::TableNextColumn();
            if(!fresh || !std::isfinite(LogState::getSignalValue(LogSignal(i),m)) || (i==SIG_AFR && (narrowband || !m.measuredAfr))) ImGui::TextDisabled("--");
            else ImGui::Text("%.2f",LogState::getSignalValue(LogSignal(i),m));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(SIGNAL_DEFS[i].unit);
        }
        ImGui::EndTable();
    }
    }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}
std::string InlineDashboardText(const MonitorData& m,bool fresh,float stoichAfr) {
    if(!fresh)
        return "RPM -- | TPS -- | MAP -- | ECT -- | Battery -- | AFR -- | Mixture -- | VE -- | Ign Timing (base) -- | STFT -- | Trigger Sync -- | Sync Losses --";

    char afr[24]="--";
    const char* mixture="--";
    if(m.measuredAfr>0) {
        snprintf(afr,sizeof(afr),"%.1f",m.measuredAfr);
        // Allow one telemetry step either side of stoichiometric AFR.
        mixture=m.measuredAfr>stoichAfr+0.1001f?"Lean":
                m.measuredAfr<stoichAfr-0.1001f?"Rich":"Stoich";
    } else {
        // Use the ECU's calibrated narrowband classification; do not invent AFR.
        switch(m.narrowbandBand) {
        case 1: mixture="Lean"; break;
        case 2: mixture="Stoich"; break;
        case 4: mixture="Rich"; break;
        }
    }
    char text[512];
    snprintf(text,sizeof(text),
             "RPM %u | TPS %.1f%% | MAP %u kPa | ECT %d C | Battery %.1f V | AFR %s | Mixture %s | VE %u%% | Ign Timing (base) %.1f deg | STFT %+.1f%% | Trigger Sync %s | Sync Losses %u%s",
             m.rpm,m.tps,m.kpa,m.clt,m.battery,afr,mixture,m.ve,m.advance,m.trimPercent,
             m.synced()?"OK":"lost",m.lossOfSyncCount,m.lossOfSyncCount==65535?"+":"");
    return text;
}
