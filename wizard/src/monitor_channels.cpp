#include "monitor_channels.h"
#include <cctype>

const SignalDef SIGNAL_DEFS[SIG_COUNT] = {
    {"Engine Speed","rpm",IM_COL32(255,214,80,255),0,8000, "engine_speed", "RPM", "RPM", "Crankshaft speed.", false},
    {"Manifold Absolute Pressure","kPa",IM_COL32(95,195,255,255),0,300, "manifold_pressure", "MAP", "MAP", "Absolute intake manifold pressure, not gauge boost pressure.", false},
    {"Throttle Position","%",IM_COL32(186,246,142,255),0,100, "throttle_position", "TPS", "TPS", "Calibrated throttle opening.", false},
    {"AFR Actual","AFR",IM_COL32(255,125,135,255),7,22, "afr_actual", "AFR Actual", "Measured AFR|AFR", "Measured wideband air-fuel ratio. Zero means unavailable.", false},
    {"AFR Target","AFR",IM_COL32(117,237,204,255),7,22, "afr_target", "AFR Target", "Target AFR", "Requested air-fuel ratio.", false},
    {"Ignition Timing - Base Command","deg",IM_COL32(170,145,235,255),-20,50, "ignition_timing", "Ign Timing", "Planned advance|Ign timing|Base planned advance", "Base planned ignition advance; does not confirm delivered spark or include requested knock retard.", false},
    {"Volumetric Efficiency","%",IM_COL32(223,228,91,255),0,255, "volumetric_efficiency", "VE", "VE", "Interpolated fuel-model volumetric efficiency.", false},
    {"Coolant Temperature","C",IM_COL32(101,136,122,255),-40,150, "coolant_temperature", "ECT", "CLT|ECT|Coolant", "Engine coolant temperature.", false},
    {"Intake Air Temperature","C",IM_COL32(154,219,153,255),-40,150, "intake_air_temperature", "IAT", "IAT|Intake air", "Intake air temperature.", false},
    {"Battery Voltage","V",IM_COL32(207,127,184,255),0,20, "battery_voltage", "Battery", "Battery|VBATT", "Measured ECU supply voltage.", false},
    {"Injector Pulse Width - Commanded","ms",IM_COL32(85,210,215,255),0,30, "injector_pulse_width", "Inj PW", "Planned injector PW|Planned pulse width", "Planned injector pulse duration; cuts and output inhibits can prevent injection.", false},
    {"Short Term Fuel Trim","%",IM_COL32(138,118,246,255),-50,50, "short_term_fuel_trim", "STFT", "Applied trim|STFT|STFT correction", "Applied short-term fuel correction. Positive adds fuel, negative removes fuel.", false},
    {"O2 Sensor Voltage","mV",IM_COL32(191,201,102,255),0,1275, "oxygen_voltage", "O2 Voltage", "Oxygen input", "Raw oxygen-sensor input voltage, not an AFR measurement.", false},
    {"Idle Valve Position","steps",IM_COL32(244,109,133,255),0,220, "idle_valve_position", "Idle Position", "IAC position|IAC", "Reported stepper position; not independent physical feedback.", false},
    {"Idle Speed Target","rpm",IM_COL32(122,192,164,255),0,2550, "idle_speed_target", "Idle Target", "Idle target", "Requested idle engine speed.", false},
    {"Vehicle Speed","km/h",IM_COL32(175,100,195,255),0,255, "vehicle_speed", "Speed", "Speed", "Calculated vehicle speed.", false},
    {"Gear Position","",IM_COL32(228,183,226,255),0,15, "gear_position", "Gear", "Gear", "Reported gear position.", false},
    {"Warm-Up Enrichment","%",IM_COL32(106,91,82,255),100,255, "warmup_enrichment", "WUE", "Warm-up|WUE", "Fuel multiplier: 100% is neutral.", false},
    {"After-Start Enrichment","%",IM_COL32(159,174,113,255),100,255, "afterstart_enrichment", "ASE", "After-start|ASE", "Fuel multiplier after starting: 100% is neutral.", false},
    {"Acceleration Enrichment","%",IM_COL32(212,82,144,255),100,255, "acceleration_enrichment", "AE", "Acceleration|AE", "Transient fuel multiplier: 100% is neutral.", false},
    {"Trigger Sync","",IM_COL32(90,165,175,255),0,1, "trigger_sync", "Sync", "Sync", "Crank trigger synchronization acquired.", false},
    {"Trigger Sync Loss Count","count",IM_COL32(143,248,206,255),0,65535, "trigger_sync_losses", "Sync Losses", "Sync losses|Losses", "Loss-of-sync counter since reset; saturates at 65535.", false},
    {"Fuel Table RPM Index","",IM_COL32(196,156,237,255),0,14, "fuel_rpm_index", "RPM Index", "RPM cell", "Fuel interpolation RPM cell index.", true},
    {"Fuel Table RPM Weight","",IM_COL32(249,239,93,255),0,1, "fuel_rpm_weight", "RPM Weight", "RPM fraction", "RPM interpolation fraction, 0 to 1.", true},
    {"Fuel Table Load Index","",IM_COL32(127,147,124,255),0,14, "fuel_load_index", "Load Index", "Load cell", "Fuel interpolation load cell index.", true},
    {"Fuel Table Load Weight","",IM_COL32(180,230,155,255),0,1, "fuel_load_weight", "Load Weight", "Load fraction", "Load interpolation fraction, 0 to 1.", true},
    {"Engine State Flags","",IM_COL32(233,138,186,255),0,255, "engine_state_flags", "Engine State", "Engine state bits", "Raw engine-state bitmask; decoded engine statuses appear on the dashboard.", true},
    {"ECU Status Flags","",IM_COL32(111,221,217,255),0,255, "ecu_status_flags", "ECU Status", "Status bits", "Raw ECU-status bitmask; decoded statuses appear on the dashboard.", true},
    {"Output Inhibit Flags","",IM_COL32(164,129,248,255),0,65535, "output_inhibit_flags", "Output Inhibits", "Output inhibits", "Raw output-admission inhibit mask; dashboard lists active reasons.", true},
    {"Calibration Generation","",IM_COL32(217,212,104,255),0,65535, "calibration_generation", "Tune Generation", "Tune generation", "ECU active-calibration revision counter.", true},
    {"Narrowband Mixture Status","",IM_COL32(95,120,135,255),0,4, "narrowband_mixture_status", "NB Mixture", "Narrowband band", "Narrowband state code: 0 unavailable, 1 lean, 2 stoichiometric, 4 rich.", false},
    {"AFR Signal Valid","",IM_COL32(148,203,166,255),0,1, "afr_signal_valid", "AFR Valid", "AFR valid", "Measured AFR is available; target AFR is never substituted.", false},
    {"Injector Duty Cycle - Calculated","%",IM_COL32(180,130,255,255),0,100, "injector_duty_cycle", "Inj Duty", "Planned injector duty", "Calculated from commanded pulse width and engine speed; not output feedback.", false},
    {"Knock Signal Level","mV",IM_COL32(255,190,80,255),0,5000, "knock_signal_level", "Knock Level", "AN15 held integral|AN15", "Held knock-integrator voltage from AN15, not a calibrated acoustic knock intensity. Unavailable is shown as a gap.", false},
    {"Knock Detected","",IM_COL32(255,90,80,255),0,1, "knock_detected", "Knock", "Knock detected", "ECU knock detection status.", false},
    {"Knock Retard - Requested","deg",IM_COL32(230,150,90,255),0,12, "knock_retard", "Knock Retard", "Requested knock retard", "Requested ignition retard; does not confirm delivered spark timing.", false},
    {"Knock Signal Valid","",IM_COL32(90,200,140,255),0,1, "knock_signal_valid", "Knock Valid", "Knock voltage fresh", "Knock held-integrator reading is fresh.", false},
    {"Knock Sensor Fault","",IM_COL32(255,80,100,255),0,1, "knock_sensor_fault", "Knock Fault", "Knock sensing fault", "Knock sensing fault status.", false},
};

namespace {
std::string Lower(std::string s) {
    for(char& c:s) c=char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string BaseName(std::string name) {
    const auto unit=name.rfind('(');
    if(unit!=std::string::npos && name.back()==')') name.resize(unit);
    while(!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
    return Lower(name);
}
bool Contains(const char* text,const std::string& query) { return Lower(text).find(query)!=std::string::npos; }
}
int FindMonitorChannel(const std::string& name) {
    const std::string base=BaseName(name);
    for(int i=0;i<SIG_COUNT;++i) {
        const auto& def=SIGNAL_DEFS[i];
        if(base==Lower(def.name) || base==Lower(def.id) || base==Lower(def.shortName)) return i;
        std::string aliases=def.aliases;
        size_t start=0;
        while(start<aliases.size()) {
            const auto end=aliases.find('|',start);
            if(base==Lower(aliases.substr(start,end-start))) return i;
            if(end==std::string::npos) break;
            start=end+1;
        }
    }
    return -1;
}
bool MonitorChannelMatches(const std::string& name,const char* query) {
    const std::string needle=Lower(query);
    if(Lower(name).find(needle)!=std::string::npos) return true;
    const int index=FindMonitorChannel(name);
    if(index<0) return false;
    const auto& def=SIGNAL_DEFS[index];
    return Contains(def.name,needle) || Contains(def.shortName,needle) || Contains(def.aliases,needle) || Contains(def.id,needle);
}
std::string MonitorColumnName(int signal) {
    const auto& def=SIGNAL_DEFS[signal];
    return std::string(def.name)+(def.unit[0]?"("+std::string(def.unit)+")":"");
}
std::string MonitorShortLabel(const std::string& name) {
    const int index=FindMonitorChannel(name);
    if(index<0) return name;
    const auto unit=name.rfind('(');
    return std::string(SIGNAL_DEFS[index].shortName)+(unit!=std::string::npos?" "+name.substr(unit):"");
}
