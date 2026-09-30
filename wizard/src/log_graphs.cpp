#include "logviewer.h"
#include "graph_math.h"
#include "monitor_channels.h"
#include "imgui_freetype.h"
#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef TW_UI_PREVIEW
extern ImVec2 graphPreviewStart,graphPreviewSize;
extern bool graphPreviewControls;
#endif

namespace {
ImFont* channelFont=nullptr;
std::string Unit(const std::string& name) {
    const auto start=name.rfind('('),end=name.rfind(')');
    return start!=std::string::npos && end>start?name.substr(start+1,end-start-1):"";
}
bool Matches(const std::string& name,const char* filter) { return MonitorChannelMatches(name,filter); }
void ValueLabel(char* text,size_t size,float value) {
    if(std::abs(value)>=1000 || std::abs(value-std::round(value))<.0001f) snprintf(text,size,"%.0f",value);
    else if(std::abs(value)>=10) snprintf(text,size,"%.1f",value);
    else snprintf(text,size,"%.2f",value);
}
void FitTime(LogViewerState& v) { v.viewStart=0; v.viewEnd=v.log.duration; v.followLatest=false; }
void StopFollowing(LogViewerState& v) { v.followLatest=false; v.playing=false; }
float TickStep(float span) {
    if(!std::isfinite(span) || span<=0) return 1;
    const float base=std::pow(10.0f,std::floor(std::log10(span/5)));
    const float n=span/(5*base);
    return base*(n<=1?1:n<=2?2:n<=5?5:10);
}
void SetRange(LVSignal& s,const GraphStatistics& stats) {
    if(!stats.count) { s.yMin=0; s.yMax=1; return; }
    const float padding=std::max((stats.maximum-stats.minimum)*.05f,std::max(.05f,std::abs(stats.maximum)*.005f));
    s.yMin=stats.minimum-padding; s.yMax=stats.maximum+padding;
}
void UpdateRanges(LogViewerState& v) {
    for(auto& s:v.signals) {
        if(!s.visible) continue;
        if(s.scaleMode==0) {
            GraphStatistics stats; stats.add(s.fullMin); stats.add(s.fullMax); SetRange(s,stats);
        } else if(s.scaleMode==1) SetRange(s,SelectedGraphStatistics(v.log.time,v.log.columns[s.colIndex],
                                      v.log.columnNames[s.colIndex],v.viewStart,v.viewEnd));
        if(!std::isfinite(s.yMin) || !std::isfinite(s.yMax) || s.yMax<=s.yMin) { s.yMin=0; s.yMax=1; }
    }
    if(v.linkUnitScales) {
        for(auto& s:v.signals) {
            const std::string unit=Unit(v.log.columnNames[s.colIndex]);
            if(!s.visible || s.scaleMode==2 || unit.empty()) continue;
            float lo=s.yMin,hi=s.yMax;
            for(const auto& other:v.signals)
                if(other.visible && other.scaleMode!=2 && other.plotIndex==s.plotIndex && Unit(v.log.columnNames[other.colIndex])==unit)
                    { lo=std::min(lo,other.yMin); hi=std::max(hi,other.yMax); }
            for(auto& other:v.signals)
                if(other.visible && other.scaleMode!=2 && other.plotIndex==s.plotIndex && Unit(v.log.columnNames[other.colIndex])==unit)
                    { other.yMin=lo; other.yMax=hi; }
        }
    }
}
void DrawTrace(ImDrawList* draw,const LogViewerState& v,const LVSignal& signal,
               ImVec2 p,ImVec2 size,float gapThreshold) {
    const auto& times=v.log.time;
    const auto& values=v.log.columns[signal.colIndex];
    const auto& name=v.log.columnNames[signal.colIndex];
    const float span=v.viewEnd-v.viewStart,range=signal.yMax-signal.yMin;
    const auto point=[&](int i) { return ImVec2(p.x+(times[i]-v.viewStart)/span*size.x,
                                              p.y+size.y-(values[i]-signal.yMin)/range*size.y); };
    int first=std::max(0,int(std::lower_bound(times.begin(),times.end(),v.viewStart)-times.begin())-1);
    int last=std::min(int(times.size())-1,int(std::upper_bound(times.begin(),times.end(),v.viewEnd)-times.begin()));
    // A min/max envelope keeps brief spikes visible even in a long log.
    int bucket=-999,low=-1,high=-1,begin=-1,finish=-1;
    bool previousValid=false;
    ImVec2 previous;
    auto flush=[&]() {
        if(begin<0) return;
        const ImVec2 head=point(begin),tail=point(finish);
        if(previousValid) draw->AddLine(previous,head,signal.color,S(v.lineWidth));
        int indices[4]={begin,low,high,finish};
        std::sort(indices,indices+4);
        for(int i=1;i<4;++i) if(indices[i]!=indices[i-1])
            draw->AddLine(point(indices[i-1]),point(indices[i]),signal.color,S(v.lineWidth));
        if(begin==finish) draw->AddCircleFilled(head,S(v.lineWidth*.5f),signal.color);
        previous=tail; previousValid=true; begin=finish=low=high=-1;
    };
    for(int i=first;i<=last;++i) {
        if(i>=int(values.size()) || !GraphValueValid(name,values[i]) || (i>first && times[i]-times[i-1]>gapThreshold)) {
            flush(); previousValid=false; bucket=-999;
            if(i>=int(values.size()) || !GraphValueValid(name,values[i])) continue;
        }
        const int bin=int((times[i]-v.viewStart)/span*size.x);
        if(bin!=bucket) { flush(); bucket=bin; }
        if(begin<0) begin=low=high=i;
        if(values[i]<values[low]) low=i;
        if(values[i]>values[high]) high=i;
        finish=i;
    }
    flush();
}
void DrawPanel(LogViewerState& v,int panel,float height,float gapThreshold) {
    auto& plot=v.plots[panel];
    std::vector<int> members;
    for(int i=0;i<int(v.signals.size());++i) if(v.signals[i].visible && v.signals[i].plotIndex==panel) members.push_back(i);
    ImGui::PushID(panel);
    ImGui::TextUnformatted(plot.label);
    if(!v.fitPanels) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(S(120));
        ImGui::DragFloat("Height",&plot.height,1,100,500,"%.0f px",ImGuiSliderFlags_AlwaysClamp);
        height=S(plot.height);
    }
    if(members.empty()) { ImGui::TextDisabled("Assign a signal to this graph in Channels."); ImGui::PopID(); return; }
    if(std::find(members.begin(),members.end(),plot.axisSignal)==members.end()) plot.axisSignal=members.front();
    const int cursorIndex=NearestGraphSample(v.log.time,v.cursorTime);
    if(ImGui::BeginTable("Legend",ImGui::GetContentRegionAvail().x>S(550)?3:2,ImGuiTableFlags_SizingStretchSame)) {
        for(int index:members) {
            auto& s=v.signals[index];
            ImGui::TableNextColumn(); ImGui::PushID(index);
            const std::string& name=v.log.columnNames[s.colIndex];
            char label[192];
            const bool valid=cursorIndex>=0 && GraphValueValid(name,v.log.columns[s.colIndex][cursorIndex]);
            if(valid) { char value[32]; ValueLabel(value,sizeof(value),v.log.columns[s.colIndex][cursorIndex]); snprintf(label,sizeof(label),"%s  %s",MonitorShortLabel(name).c_str(),value); }
            else snprintf(label,sizeof(label),"%s  --",MonitorShortLabel(name).c_str());
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(s.color));
            if(ImGui::Selectable(label,plot.axisSignal==index)) plot.axisSignal=index;
            ImGui::PopStyleColor();
            if(ImGui::IsItemHovered()) {
                const int channel=FindMonitorChannel(name);
                ImGui::SetTooltip("%s\n%s\nScale %g to %g\nClick to show this trace's Y axis.",name.c_str(),
                    channel>=0?SIGNAL_DEFS[channel].description:"Imported CSV channel",s.yMin,s.yMax);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    const ImVec2 outer=ImGui::GetCursorScreenPos();
    const float width=std::max(S(120),ImGui::GetContentRegionAvail().x);
    height=std::max(height,S(90));
    const auto& axis=v.signals[plot.axisSignal];
    char loLabel[32],hiLabel[32]; snprintf(loLabel,sizeof(loLabel),"%.4g",axis.yMin); snprintf(hiLabel,sizeof(hiLabel),"%.4g",axis.yMax);
    const float axisWidth=std::max(S(56),std::max(ImGui::CalcTextSize(loLabel).x,ImGui::CalcTextSize(hiLabel).x)+S(12));
    const ImVec2 p(outer.x+axisWidth,outer.y+S(8)),size(std::max(S(40),width-axisWidth-S(12)),height-S(35));
#ifdef TW_UI_PREVIEW
    if(panel==0) { graphPreviewStart=p; graphPreviewSize=size; }
#endif
    ImGui::InvisibleButton("Plot",ImVec2(width,height),ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
    const bool hover=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
    const ImVec2 mouse=ImGui::GetIO().MousePos;
    const float fraction=std::clamp((mouse.x-p.x)/size.x,0.0f,1.0f);
    const float time=v.viewStart+fraction*(v.viewEnd-v.viewStart);
    const bool inside=hover && mouse.x>=p.x && mouse.x<=p.x+size.x && mouse.y>=p.y && mouse.y<=p.y+size.y;
    if(inside) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if(ImGui::GetIO().MouseWheel!=0) {
            StopFollowing(v); ZoomGraphWindow(v.log.duration,time,std::pow(.75f,ImGui::GetIO().MouseWheel),v.viewStart,v.viewEnd);
        }
        if(!v.cursorLocked && !v.playing) v.cursorTime=time;
        if(ImGui::IsMouseClicked(0)) {
            v.cursorTime=time; v.cursorLocked=true; v.playing=false;
            v.panPlotIdx=panel; v.panStartMouseX=mouse.x; v.panStartViewStart=v.viewStart; v.panStartViewEnd=v.viewEnd;
            if(v.dragToZoom || ImGui::GetIO().KeyShift) {
                StopFollowing(v); v.selecting=true; v.selectionPlot=panel;
                v.selectionStart=v.selectionEnd=time; v.hasSelection=true;
                v.zoomOnRelease=v.dragToZoom && !ImGui::GetIO().KeyShift;
            }
        }
        if(ImGui::IsMouseDoubleClicked(0)) { v.selecting=false; v.hasSelection=false; v.zoomOnRelease=false; FitTime(v); }
    }
    if(active && v.selecting && v.selectionPlot==panel) v.selectionEnd=time;
    else if(active && v.panPlotIdx==panel && ImGui::IsMouseDragging(0,S(3))) {
        StopFollowing(v);
        const float delta=-(mouse.x-v.panStartMouseX)/size.x*(v.panStartViewEnd-v.panStartViewStart);
        v.viewStart=v.panStartViewStart+delta; v.viewEnd=v.panStartViewEnd+delta;
        ClampGraphWindow(v.log.duration,v.viewStart,v.viewEnd);
    }
    if(!ImGui::IsMouseDown(0) && v.selecting && v.selectionPlot==panel) {
        v.selectionEnd=time;
        const bool dragged=std::abs(mouse.x-v.panStartMouseX)>=S(3);
        if(v.zoomOnRelease && dragged) {
            v.viewStart=std::min(v.selectionStart,v.selectionEnd);
            v.viewEnd=std::max(v.selectionStart,v.selectionEnd);
            ClampGraphWindow(v.log.duration,v.viewStart,v.viewEnd);
            v.cursorTime=(v.viewStart+v.viewEnd)*.5f;
        }
        if(v.zoomOnRelease || !dragged) v.hasSelection=false;
        v.selecting=false; v.zoomOnRelease=false;
    }
    if(hover && ImGui::IsMouseClicked(1)) ImGui::OpenPopup("Graph menu");
    if(ImGui::BeginPopup("Graph menu")) {
        if(ImGui::MenuItem("Fit entire log")) FitTime(v);
        if(ImGui::MenuItem("Fit visible Y ranges")) for(int index:members) v.signals[index].scaleMode=1;
        if(ImGui::MenuItem("Unlock cursor")) v.cursorLocked=false;
        if(ImGui::MenuItem("Zoom to selection",nullptr,false,v.hasSelection)) {
            StopFollowing(v); v.viewStart=std::min(v.selectionStart,v.selectionEnd); v.viewEnd=std::max(v.selectionStart,v.selectionEnd);
            ClampGraphWindow(v.log.duration,v.viewStart,v.viewEnd);
        }
        ImGui::EndPopup();
    }
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(outer,ImVec2(outer.x+width,outer.y+height),ImGui::GetColorU32(ImGuiCol_ChildBg),S(4));
    draw->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),IM_COL32(12,16,20,255));
    const float span=v.viewEnd-v.viewStart;
    auto x=[&](float t) { return p.x+(t-v.viewStart)/span*size.x; };
    const float yStep=TickStep(axis.yMax-axis.yMin);
    for(float value=std::ceil(axis.yMin/yStep)*yStep;value<=axis.yMax;value+=yStep) {
        const float y=p.y+size.y-(value-axis.yMin)/(axis.yMax-axis.yMin)*size.y;
        if(v.showGrid) draw->AddLine(ImVec2(p.x,y),ImVec2(p.x+size.x,y),IM_COL32(60,70,80,150));
        char label[32]; snprintf(label,sizeof(label),"%.4g",value);
        draw->AddText(ImVec2(p.x-ImGui::CalcTextSize(label).x-S(8),y-ImGui::GetFontSize()*.5f),axis.color,label);
    }
    const float step=TickStep(span);
    for(float t=std::ceil(v.viewStart/step)*step;t<=v.viewEnd;t+=step) {
        if(v.showGrid) draw->AddLine(ImVec2(x(t),p.y),ImVec2(x(t),p.y+size.y),IM_COL32(60,70,80,150));
        char label[32]; snprintf(label,sizeof(label),"%.3g s",t);
        const float tx=std::clamp(x(t)-ImGui::CalcTextSize(label).x*.5f,p.x,p.x+size.x-ImGui::CalcTextSize(label).x);
        draw->AddText(ImVec2(tx,p.y+size.y+S(5)),ImGui::GetColorU32(ImGuiCol_TextDisabled),label);
    }
    draw->PushClipRect(p,ImVec2(p.x+size.x,p.y+size.y),true);
    if(v.hasSelection) draw->AddRectFilled(ImVec2(x(std::min(v.selectionStart,v.selectionEnd)),p.y),ImVec2(x(std::max(v.selectionStart,v.selectionEnd)),p.y+size.y),IM_COL32(100,180,230,45));
    for(int index:members) DrawTrace(draw,v,v.signals[index],p,size,gapThreshold);
    if(v.cursorTime>=v.viewStart && v.cursorTime<=v.viewEnd) {
        draw->AddLine(ImVec2(x(v.cursorTime),p.y),ImVec2(x(v.cursorTime),p.y+size.y),ImGui::GetColorU32(ImGuiCol_PlotHistogram),S(1.5f));
        for(int index:members) {
            const auto& s=v.signals[index]; const float value=v.log.columns[s.colIndex][cursorIndex];
            if(GraphValueValid(v.log.columnNames[s.colIndex],value)) draw->AddCircleFilled(ImVec2(x(v.log.time[cursorIndex]),p.y+size.y-(value-s.yMin)/(s.yMax-s.yMin)*size.y),S(3),s.color);
        }
    }
    draw->PopClipRect();
    ImGui::PopID();
}
void SelectionStats(LogViewerState& v) {
    if(!v.hasSelection || v.selecting || !ImGui::CollapsingHeader("Selected interval statistics",ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Text("%.3f to %.3f s / %.3f s",std::min(v.selectionStart,v.selectionEnd),std::max(v.selectionStart,v.selectionEnd),std::abs(v.selectionEnd-v.selectionStart));
    ImGui::SameLine();
    if(ImGui::SmallButton("Zoom selection")) { StopFollowing(v); v.viewStart=std::min(v.selectionStart,v.selectionEnd); v.viewEnd=std::max(v.selectionStart,v.selectionEnd); ClampGraphWindow(v.log.duration,v.viewStart,v.viewEnd); }
    ImGui::SameLine(); if(ImGui::SmallButton("Clear selection")) v.hasSelection=false;
    if(ImGui::BeginTable("Statistics",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_ScrollY,ImVec2(0,S(110)))) {
        for(const char* column:{"Signal","Minimum","Maximum","Average"}) ImGui::TableSetupColumn(column);
        ImGui::TableHeadersRow();
        for(const auto& s:v.signals) if(s.visible) {
            const auto stats=SelectedGraphStatistics(v.log.time,v.log.columns[s.colIndex],v.log.columnNames[s.colIndex],v.selectionStart,v.selectionEnd);
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(s.color),"%s",v.log.columnNames[s.colIndex].c_str());
            for(double value:{double(stats.minimum),double(stats.maximum),stats.average()}) { ImGui::TableNextColumn(); if(stats.count) ImGui::Text("%.4g",value); else ImGui::TextDisabled("--"); }
        }
        ImGui::EndTable();
    }
}
}

void LoadLogViewerFonts(float dpiScale) {
    ImFontConfig config; config.PixelSnapH=true;
    config.FontBuilderFlags=ImGuiFreeTypeBuilderFlags_ForceAutoHint;
    channelFont=ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",13*dpiScale,&config);
    if(!channelFont) { config.SizePixels=13*dpiScale; channelFont=ImGui::GetIO().Fonts->AddFontDefault(&config); }
}

void DrawLogViewer(LogViewerState& v) {
    if(!v.parseOk || v.log.sampleCount<2) { ImGui::TextDisabled("Waiting for at least two accepted samples."); return; }
    ImGui::PushID(&v);
    if(v.plots.empty()) { LVPlot plot; strcpy(plot.label,"Graph 1"); v.plots.push_back(plot); }
    ClampGraphWindow(v.log.duration,v.viewStart,v.viewEnd);
    if(v.playing) {
        v.cursorTime=std::min(v.log.duration,v.cursorTime+ImGui::GetIO().DeltaTime*v.playbackRate);
        v.cursorLocked=true;
        if(v.cursorTime>=v.log.duration) v.playing=false;
        if(v.cursorTime>v.viewEnd || v.cursorTime<v.viewStart) { const float span=v.viewEnd-v.viewStart; v.viewStart=v.cursorTime-span*.2f; v.viewEnd=v.viewStart+span; ClampGraphWindow(v.log.duration,v.viewStart,v.viewEnd); }
    }
    if(v.liveMode) {
        if(ImGui::Button(v.followLatest?"Pause view":"Follow live")) { v.followLatest=!v.followLatest; if(v.followLatest) { v.cursorLocked=false; v.hasSelection=false; } }
    } else {
        if(ImGui::Button(v.playing?"Pause":"Play")) { if(v.cursorTime>=v.log.duration) v.cursorTime=0; v.playing=!v.playing; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(S(95)); ImGui::SliderFloat("Speed",&v.playbackRate,.1f,8,"%.1fx");
    }
    ImGui::SameLine(); if(ImGui::Button("Fit time")) FitTime(v);
    ImGui::SameLine(); if(ImGui::Button("Zoom +")) { StopFollowing(v); ZoomGraphWindow(v.log.duration,v.cursorTime,.5f,v.viewStart,v.viewEnd); }
    ImGui::SameLine(); if(ImGui::Button("Zoom -")) { StopFollowing(v); ZoomGraphWindow(v.log.duration,v.cursorTime,2,v.viewStart,v.viewEnd); }
    ImGui::SameLine(); ImGui::Checkbox("Fit graph heights",&v.fitPanels);
    ImGui::SetNextItemWidth(std::min(S(300),ImGui::GetContentRegionAvail().x*.45f));
    if(ImGui::SliderFloat("Cursor / time",&v.cursorTime,0,v.log.duration,"%.3f s")) { StopFollowing(v); v.cursorLocked=true; }
    ImGui::SameLine(); ImGui::Checkbox("Lock cursor",&v.cursorLocked);
    if(v.liveMode) { ImGui::SameLine(); ImGui::SetNextItemWidth(S(110)); ImGui::DragFloat("Window",&v.liveWindow,1,1,600,"%.1f s",ImGuiSliderFlags_AlwaysClamp); }
    ImGui::Checkbox("Drag to zoom",&v.dragToZoom); ImGui::SameLine();
    ImGui::TextWrapped("Wheel: zoom | %s | Shift+drag: interval statistics | Click: cursor | Double-click: fit time",
                       v.dragToZoom?"Drag: select and zoom":"Drag: pan");
    SelectionStats(v);
    const float available=ImGui::GetContentRegionAvail().x;
    const float sidebar=std::min(S(270),available*.4f);
    ImGui::BeginChild("Channels",ImVec2(sidebar,0),ImGuiChildFlags_Borders);
    ImGui::Text("Channels / %.2f s / %d samples",v.log.duration,v.log.sampleCount);
    if(v.log.skippedRows) {
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram),"%d import rows skipped",v.log.skippedRows);
        if(ImGui::TreeNode("Import details")) { for(const auto& detail:v.log.diagnostics) ImGui::TextWrapped("%s",detail.c_str()); ImGui::TreePop(); }
    }
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##Filter","Search channels...",v.signalFilter,sizeof(v.signalFilter));
    if(ImGui::SmallButton("Show filtered")) for(auto& s:v.signals) if(Matches(v.log.columnNames[s.colIndex],v.signalFilter)) s.visible=true;
    ImGui::SameLine(); if(ImGui::SmallButton("Hide filtered")) for(auto& s:v.signals) if(Matches(v.log.columnNames[s.colIndex],v.signalFilter)) s.visible=false;
#ifdef TW_UI_PREVIEW
    if(graphPreviewControls) ImGui::SetNextItemOpen(true,ImGuiCond_Once);
#endif
    if(ImGui::CollapsingHeader("Graph configuration")) {
        ImGui::Checkbox("Grid",&v.showGrid); ImGui::Checkbox("Link scales with matching units",&v.linkUnitScales);
        ImGui::TextDisabled("Trace width"); ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##Trace width",&v.lineWidth,1,5,"%.1f");
        if(v.plots.size()<8 && ImGui::SmallButton("Add graph")) { LVPlot plot; snprintf(plot.label,sizeof(plot.label),"Graph %d",int(v.plots.size())+1); v.plots.push_back(plot); }
        if(v.plots.size()>1) { ImGui::SameLine(); if(ImGui::SmallButton("Remove last")) { v.plots.pop_back(); for(auto& s:v.signals) s.plotIndex=std::min(s.plotIndex,int(v.plots.size())-1); } }
        for(int p=0;p<int(v.plots.size());++p) { ImGui::PushID(p+5000); ImGui::SetNextItemWidth(-1); ImGui::InputText("##Name",v.plots[p].label,sizeof(v.plots[p].label)); ImGui::PopID(); }
    }
    const int nearest=NearestGraphSample(v.log.time,v.cursorTime);
    if(channelFont) ImGui::PushFont(channelFont);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(S(2),S(1)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(S(4),S(2)));
    const bool searching=v.signalFilter[0]!=0;
    bool selectedAdvanced=false;
    for(const auto& s:v.signals) { const int channel=FindMonitorChannel(v.log.columnNames[s.colIndex]); if(s.visible && channel>=0 && SIGNAL_DEFS[channel].advanced) selectedAdvanced=true; }
    for(int pass=0;pass<2;++pass) {
    if(pass==1 && !searching && !ImGui::CollapsingHeader("Advanced / ECU Status",selectedAdvanced?ImGuiTreeNodeFlags_DefaultOpen:0)) continue;
    for(int i=0;i<int(v.signals.size());++i) {
        auto& s=v.signals[i]; const auto& name=v.log.columnNames[s.colIndex];
        const int channel=FindMonitorChannel(name);
        const bool advanced=channel>=0 && SIGNAL_DEFS[channel].advanced;
        if(advanced!=(pass==1)) continue;
        if(!Matches(name,v.signalFilter)) continue;
        s.plotIndex=std::clamp(s.plotIndex,0,int(v.plots.size())-1);
        ImGui::PushID(i);
        ImGui::Checkbox("##visible",&s.visible); ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(s.color));
#ifdef TW_UI_PREVIEW
        if(graphPreviewControls && i==0) ImGui::SetNextItemOpen(true,ImGuiCond_Once);
#endif
        const bool details=ImGui::TreeNodeEx("##details",ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_NoTreePushOnOpen,"");
        ImGui::SameLine(); ImGui::TextWrapped("%s",name.c_str());
        ImGui::PopStyleColor();
        if(ImGui::IsItemHovered() && channel>=0) ImGui::SetTooltip("%s\nAliases: %s",SIGNAL_DEFS[channel].description,SIGNAL_DEFS[channel].aliases);
        if(s.visible) { if(GraphValueValid(name,v.log.columns[s.colIndex][nearest])) ImGui::Text("%.4g  /  %s",v.log.columns[s.colIndex][nearest],v.plots[s.plotIndex].label); else ImGui::TextDisabled("-- / %s",v.plots[s.plotIndex].label); }
        if(details) {
            ImGui::TreePush("##settings");
            ImGui::TextDisabled("Graph"); ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##Graph",v.plots[s.plotIndex].label)) { for(int p=0;p<int(v.plots.size());++p) if(ImGui::Selectable(v.plots[p].label,s.plotIndex==p)) s.plotIndex=p; ImGui::EndCombo(); }
            const char* modes[]={"Full log range","Visible range","Manual range"};
            ImGui::TextDisabled("Scale"); ImGui::SetNextItemWidth(-1); ImGui::Combo("##Scale",&s.scaleMode,modes,3);
            if(s.scaleMode==2) {
                if(!s.rangeInitialized) { s.manualMin=s.yMin; s.manualMax=s.yMax; s.rangeInitialized=true; }
                ImGui::TextDisabled("Minimum"); ImGui::SetNextItemWidth(-1); bool commit=ImGui::InputFloat("##Minimum",&s.manualMin,0,0,"%.4g",ImGuiInputTextFlags_EnterReturnsTrue);
                commit|=ImGui::IsItemDeactivatedAfterEdit();
                ImGui::TextDisabled("Maximum"); ImGui::SetNextItemWidth(-1); commit|=ImGui::InputFloat("##Maximum",&s.manualMax,0,0,"%.4g",ImGuiInputTextFlags_EnterReturnsTrue);
                commit|=ImGui::IsItemDeactivatedAfterEdit();
                const bool valid=std::isfinite(s.manualMin) && std::isfinite(s.manualMax) && s.manualMax>s.manualMin;
                if(commit && valid) { s.yMin=s.manualMin; s.yMax=s.manualMax; }
                if(!valid) ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram),"Maximum must exceed minimum.");
            } else s.rangeInitialized=false;
            ImVec4 color=ImGui::ColorConvertU32ToFloat4(s.color);
            if(ImGui::ColorEdit3("Color",&color.x,ImGuiColorEditFlags_NoInputs)) s.color=ImGui::ColorConvertFloat4ToU32(color);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    }
    ImGui::PopStyleVar(2);
    if(channelFont) ImGui::PopFont();
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("Graphs",ImVec2(0,0));
    UpdateRanges(v);
    std::vector<int> panels;
    for(int p=0;p<int(v.plots.size());++p) if(std::any_of(v.signals.begin(),v.signals.end(),[&](const LVSignal& s){return s.visible && s.plotIndex==p;})) panels.push_back(p);
    if(panels.empty()) ImGui::TextDisabled("Select channels to display their traces.");
    const float height=std::max(S(90),(ImGui::GetContentRegionAvail().y-S(75)*panels.size())/std::max(1,int(panels.size())));
    // The median cadence distinguishes an interruption from a deliberately slow log.
    float gap=.5f;
    if(v.log.time.size()>2) {
        std::vector<float> cadence;
        const size_t stride=std::max(size_t(1),v.log.time.size()/512);
        for(size_t i=1;i<v.log.time.size();i+=stride) cadence.push_back(v.log.time[i]-v.log.time[i-1]);
        std::nth_element(cadence.begin(),cadence.begin()+cadence.size()/2,cadence.end());
        gap=std::max(.5f,cadence[cadence.size()/2]*5);
    }
    for(int panel:panels) { DrawPanel(v,panel,height,gap); ImGui::Spacing(); }
    ImGui::EndChild();
    ImGui::PopID();
}
