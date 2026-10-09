#include "monitor_channels.h"
#include "protocol.h"
#include <cmath>
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
    {"Idle Valve Target","steps",IM_COL32(250,150,170,255),0,220, "idle_valve_target", "Idle Valve Target", "IAC target", "Idle valve position the idle controller requests. Differs from the position while the valve moves or is not ready.", false},
    {"Idle Valve State","",IM_COL32(235,120,90,255),0,3, "idle_valve_state", "IAC State", "IAC state", "0 not referenced, 1 homing, 2 ready, 3 fault. Idle feedback only moves the valve when ready.", false},
    {"Idle Valve Fault","",IM_COL32(255,70,70,255),0,7, "idle_valve_fault", "IAC Fault", "IAC fault", "Current stepper-driver fault: 0 none, 1 SPI transfer, 2 open load, 3 driver diagnosis 10b, 4 driver diagnosis 00b, 5 homing timeout, 6 bridges not off, 7 hold timing.", false},
    {"Idle Valve Last Fault","",IM_COL32(220,90,90,255),0,7, "idle_valve_last_fault", "IAC Last Fault", "IAC last fault", "Most recent stepper-driver fault since ECU reset, kept after recovery. Codes as Idle Valve Fault.", false},
    {"Idle Valve Recoveries","count",IM_COL32(240,170,110,255),0,255, "idle_valve_recoveries", "IAC Recoveries", "IAC retries", "Automatic driver recoveries since ECU reset; saturates at 255.", false},
    {"Idle Valve Fault Count","count",IM_COL32(200,110,130,255),0,255, "idle_valve_fault_count", "IAC Faults", "IAC fault count", "Stepper-driver faults since ECU reset; saturates at 255.", true},
    {"Idle Driver Response","",IM_COL32(160,140,200,255),0,255, "idle_driver_response", "IAC Response", "L9935 response", "Last raw stepper-driver (L9935) SPI response byte; 192 is the normal no-fault reply.", true},
    {"Idle Control Mode","",IM_COL32(130,200,230,255),0,5, "idle_control_mode", "Idle Mode", "Idle mode", "0 unavailable, 1 cranking, 2 start catch, 3 feedback, 4 overrun return, 5 throttle open.", false},
    {"Idle Integral","steps",IM_COL32(110,180,250,255),-220,220, "idle_integral", "Idle Integral", "Idle integrator", "Idle feedback correction added to the base valve position.", false},
    {"Idle Spark Correction","deg",IM_COL32(190,160,250,255),-20,20, "idle_spark_correction", "Idle Spark", "Idle spark", "Idle speed ignition correction added to base timing; negative retards.", false},
    {"Sync Loss Reason","",IM_COL32(255,120,60,255),0,6, "sync_loss_reason", "Sync Loss Reason", "Loss reason", "Cause of the most recent counted sync loss: 0 none, 1 gap too early, 2 gap missing, 3 irregular tooth, 4 noise edge, 5 capture missed edge, 6 capture fault. 4 and 5 point to electrical interference.", false},
    {"Sync Loss Tooth","",IM_COL32(255,170,90,255),0,60, "sync_loss_tooth", "Sync Loss Tooth", "Loss tooth", "Tooth count after the gap at the most recent counted sync loss.", false},
    {"First Inhibit Reason","",IM_COL32(170,140,240,255),0,65535, "first_inhibit_reason", "First Inhibit", "First inhibit", "Output-inhibit bits of the first inhibit since ECU reset.", true},
    {"Crank Capture Resyncs","count",IM_COL32(120,220,200,255),0,65535, "crank_capture_resyncs", "Capture Resyncs", "Capture resyncs", "Crank edges counted but not captured (two edges within one interrupt latency) since reset. Rising during running indicates crank-signal interference.", true},
    {"Crank Capture Overruns","count",IM_COL32(100,200,180,255),0,65535, "crank_capture_overruns", "Capture Overruns", "Capture overruns", "Crank capture buffer overruns since reset.", true},
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

float MonitorSignalValue(int sig,const MonitorData& mon) {
    switch(sig) {
    case SIG_RPM: return float(mon.rpm);
    case SIG_MAP: return float(mon.kpa);
    case SIG_TPS: return float(mon.tps);
    case SIG_AFR: return float(mon.measuredAfr);
    case SIG_TARGET_AFR: return float(mon.targetAfr);
    case SIG_TIMING: return float(mon.advance);
    case SIG_VE: return float(mon.ve);
    case SIG_CLT: return float(mon.clt);
    case SIG_IAT: return float(mon.iat);
    case SIG_VBATT: return float(mon.battery);
    case SIG_INJ_PW: return float(mon.pulseUs*0.001f);
    case SIG_STFT: return float(mon.trimPercent);
    case SIG_OXYGEN: return float(mon.oxygenMv);
    case SIG_IAC_ACTUAL: return float(mon.iacPosition);
    case SIG_TARGET_IDLE: return float(mon.idleTarget);
    case SIG_SPEED: return float(mon.speed);
    case SIG_GEAR: return float(mon.gear);
    case SIG_WARMUP_ENRICH: return float(mon.warmup);
    case SIG_ASE: return float(mon.afterstart);
    case SIG_ACCEL_ENRICH: return float(mon.accel);
    case SIG_SYNC: return float(mon.synced());
    case SIG_SYNC_LOSS: return float(mon.lossOfSyncCount);
    case SIG_CELL_RPM: return float(mon.cellRpm);
    case SIG_RPM_FRACTION: return float(mon.rpmFraction);
    case SIG_CELL_LOAD: return float(mon.cellLoad);
    case SIG_LOAD_FRACTION: return float(mon.loadFraction);
    case SIG_STATE: return float(mon.state);
    case SIG_FLAGS: return float(mon.flags);
    case SIG_INHIBITS: return float(mon.inhibits);
    case SIG_GENERATION: return float(mon.generation);
    case SIG_NB_BAND: return float(mon.narrowbandBand);
    case SIG_AFR_VALID: return float(mon.measuredAfr>0);
    case SIG_INJ_DUTY: return mon.plannedDutyPercent();
    case SIG_KNOCK_MV: return mon.knockAvailable && mon.knockMv!=65535 ? float(mon.knockMv) : NAN;
    case SIG_KNOCK_DETECTED: return mon.knockAvailable ? float((mon.knockFlags&1)!=0) : NAN;
    case SIG_KNOCK_RETARD: return mon.knockAvailable ? mon.knockRetard : NAN;
    case SIG_KNOCK_VALID: return mon.knockAvailable ? float((mon.knockFlags&2)!=0) : NAN;
    case SIG_KNOCK_FAULT: return mon.knockAvailable ? float((mon.knockFlags&16)!=0) : NAN;
    case SIG_IAC_TARGET: return mon.detailAvailable ? float(mon.iacTarget) : NAN;
    case SIG_IAC_STATE: return mon.detailAvailable ? float(mon.iacState) : NAN;
    case SIG_IAC_FAULT: return mon.detailAvailable ? float(mon.iacFault) : NAN;
    case SIG_IAC_LAST_FAULT: return mon.detailAvailable ? float(mon.iacLastFault) : NAN;
    case SIG_IAC_RECOVERIES: return mon.detailAvailable ? float(mon.iacRetries) : NAN;
    case SIG_IAC_FAULT_COUNT: return mon.detailAvailable ? float(mon.iacFaultCount) : NAN;
    case SIG_IAC_RESPONSE: return mon.detailAvailable ? float(mon.iacResponse) : NAN;
    case SIG_IDLE_MODE: return mon.detailAvailable ? float(mon.idleMode) : NAN;
    case SIG_IDLE_INTEGRAL: return mon.detailAvailable ? mon.idleIntegral : NAN;
    case SIG_IDLE_SPARK: return mon.detailAvailable ? mon.idleSpark : NAN;
    case SIG_SYNC_LOSS_REASON: return mon.detailAvailable ? float(mon.syncLossReason) : NAN;
    case SIG_SYNC_LOSS_TOOTH: return mon.detailAvailable ? float(mon.syncLossTooth) : NAN;
    case SIG_FIRST_INHIBIT: return mon.detailAvailable ? float(mon.firstInhibit) : NAN;
    case SIG_CAPTURE_RESYNCS: return mon.detailAvailable ? float(mon.captureResyncs) : NAN;
    case SIG_CAPTURE_OVERRUNS: return mon.detailAvailable ? float(mon.captureOverruns) : NAN;
    default: return 0;
    }
}
