#include "cal_navigation.h"
#include <algorithm>
#include <cctype>
#include <cstring>

const CalibrationPage CALIBRATION_PAGES[] = {
    {"fuel.tables","Tuning","Fuel","","Fuel Settings"},
    {"fuel.closed_loop","Tuning","Fuel","Closed-Loop Fuel Control","Settings"},
    {"fuel.acceleration","Tuning","Fuel","Acceleration Enrichment","Settings"},
    {"fuel.starting","Tuning","Fuel","Starting & Warm-Up","Settings"},
    {"ignition.main","Tuning","Ignition","","Ignition Settings"},
    {"idle.main","Tuning","Idle Control","","Controller Settings"},
    {"boost.main","Tuning","Boost & Motorsport","Boost Control","Settings"},
    {"motorsport.main","Tuning","Boost & Motorsport","Launch & Anti-Lag","Settings"},
    {"protection.limits","Tuning","Engine Protection","Rev Limit & Fuel Cut","Settings"},
    {"protection.knock","Tuning","Engine Protection","Knock Control","Settings"},
    {"setup.fuel","Setup","Engine & Fuel System","Fuel Model","Settings"},
    {"setup.injectors","Setup","Engine & Fuel System","Injector Setup","Settings"},
    {"setup.trigger","Setup","Engine & Fuel System","Trigger Reference","Settings"},
    {"setup.state","Setup","Engine & Fuel System","Cranking & Running Detection","Settings"},
    {"sensors.tps","Setup","Sensors & Calibration","TPS Calibration","Settings"},
    {"sensors.main","Setup","Sensors & Calibration","MAP & Temperature Sensors","Settings"},
    {"sensors.oxygen","Setup","Sensors & Calibration","O2 & Wideband","Settings"},
    {"sensors.filters","Setup","Sensors & Calibration","Input Filters","Settings"},
    {"sensors.vehicle","Setup","Sensors & Calibration","Vehicle Speed & Gear","Settings"},
    {"outputs.fan","Setup","Auxiliary Outputs","Cooling Fan","Settings"},
    {"outputs.pump","Setup","Auxiliary Outputs","Fuel Pump","Settings"},
    {"outputs.gauge","Setup","Auxiliary Outputs","Coolant Gauge","Settings"},
    {"advanced.axes","Setup","Advanced","Shared Table Axes",""},
    {"advanced.timing","Setup","Advanced","Timing & Freshness Limits","Settings"},
    {"dtc.thresholds","Diagnostics","Diagnostics & DTCs","DTC Thresholds","Settings"},
    {"dtc.monitors","Diagnostics","Diagnostics & DTCs","Monitor Enables","Settings"},
    {"dtc.codes","Diagnostics","Diagnostics & DTCs","Individual Code Enables","Settings"},
};
const int NUM_CALIBRATION_PAGES=sizeof(CALIBRATION_PAGES)/sizeof(CalibrationPage);
const CalibrationPage* FindCalibrationPage(const char* id) {
    for(const auto& page:CALIBRATION_PAGES) if(!strcmp(page.id,id)) return &page;
    return nullptr;
}
const CalibrationPage* CalibrationPageFor(const char* category,const char* name,int offset) {
    const char* id=nullptr;
    if(!strcmp(category,"Fuel")) id="setup.fuel";
    if(!strcmp(category,"Fuel") && (offset==0 || offset==0x200)) id="fuel.tables";
    if(!strcmp(category,"Closed loop (STFT)")) id="fuel.closed_loop";
    if(!strcmp(category,"Fuel - acceleration")) id="fuel.acceleration";
    if(!strcmp(category,"Fuel - start and warm-up")) id="fuel.starting";
    if(!strcmp(category,"Ignition")) id=!strcmp(name,"Trigger reference offset")?"setup.trigger":"ignition.main";
    if(!strcmp(category,"Idle")) id="idle.main";
    if(!strcmp(category,"Injectors")) id="setup.injectors";
    if(!strcmp(category,"Engine state")) id=!strcmp(name,"Plan age limit")?"advanced.timing":"setup.state";
    if(!strcmp(category,"Sensors")) {
        id="sensors.main";
        if(strstr(name,"filter")) id="sensors.filters";
        if(!strcmp(name,"Sensor freshness limit")) id="advanced.timing";
    }
    if(!strcmp(category,"Oxygen sensor and wideband")) id="sensors.oxygen";
    if(!strcmp(category,"TPS calibration")) id="sensors.tps";
    if(!strcmp(category,"Vehicle speed and gear")) id="sensors.vehicle";
    if(!strcmp(category,"Fan, pump, boost and gauge")) {
        if(strstr(name,"Boost")) id="boost.main";
        else if(strstr(name,"Fan")) id="outputs.fan";
        else if(strstr(name,"pump")) id="outputs.pump";
        else if(strstr(name,"gauge")) id="outputs.gauge";
    }
    if(!strcmp(category,"Knock")) id="protection.knock";
    if(!strcmp(category,"Rev limit and fuel cut")) id="protection.limits";
    if(!strcmp(category,"Launch and anti-lag")) id="motorsport.main";
    if(!strcmp(category,"Axis breakpoints")) id="advanced.axes";
    if(!strcmp(category,"DTC thresholds")) id="dtc.thresholds";
    if(!strcmp(category,"DTC switches")) id="dtc.monitors";
    if(!strcmp(category,"DTC switches - individual codes")) id="dtc.codes";
    return id?FindCalibrationPage(id):nullptr;
}
bool CalibrationPageMatches(const char* filter,const char* category,const char* name,int offset) {
    // Retain old category tabs and visual-audit fixtures; new sidebar uses page IDs.
    if(!FindCalibrationPage(filter)) return !strcmp(filter,category);
    const auto* page=CalibrationPageFor(category,name,offset);
    return page && !strcmp(page->id,filter);
}
const char* CalibrationTableLabel(int offset,const char* original) {
    switch(offset) {
    case 0x000: return "Fuel VE Table";
    case 0x200: return "AFR Target Table";
    case 0x100: return "Ignition Timing Table";
    case 0x4D0: return "Idle Speed Target";
    case 0x4F0: return "Base Idle Valve Position";
    case 0x620: return "Idle Ignition Correction";
    case 0x480: return "Warm-Up Enrichment";
    case 0x4B0: return "After-Start Enrichment";
    case 0x759: return "Acceleration Enrichment";
    default: return original;
    }
}
bool CalibrationSearchMatches(const char* query,const char* category,const char* name,int offset) {
    if(!query[0]) return true;
    const auto* page=CalibrationPageFor(category,name,offset);
    std::string text=std::string(category)+" "+name+" "+CalibrationTableLabel(offset,name);
    if(page) text+=" "+std::string(page->group)+" "+page->label+" "+page->id;
    auto lower=[](unsigned char c){return char(std::tolower(c));};
    std::string lowerName=name;
    std::transform(lowerName.begin(),lowerName.end(),lowerName.begin(),lower);
    // Common tuning abbreviations also find their full-name destinations.
    if(lowerName.find("coolant")!=std::string::npos) text+=" ECT CLT";
    if(lowerName.find("intake")!=std::string::npos || lowerName.find("iat")!=std::string::npos) text+=" IAT intake air";
    if(strstr(name,"Warm-up")) text+=" WUE";
    if(strstr(name,"After-start")) text+=" ASE";
    if(strstr(category,"acceleration")) text+=" AE";
    if(strstr(name,"Ignition")) text+=" spark timing";
    std::string needle=query;
    std::transform(text.begin(),text.end(),text.begin(),lower);
    std::transform(needle.begin(),needle.end(),needle.begin(),lower);
    // Avoid finding "ECT" inside "injector" or "protection".
    const char* abbreviations[]={"ect","clt","iat","tps","map","rpm","ve","ae","ase","wue","stft","dtc"};
    for(const char* abbreviation:abbreviations) if(needle==abbreviation) {
        size_t pos=0;
        while((pos=text.find(needle,pos))!=std::string::npos) {
            const auto word=[](unsigned char c){return std::isalnum(c) || c=='_';};
            if((pos==0 || !word(text[pos-1])) && (pos+needle.size()==text.size() || !word(text[pos+needle.size()]))) return true;
            ++pos;
        }
        return false;
    }
    return text.find(needle)!=std::string::npos;
}
