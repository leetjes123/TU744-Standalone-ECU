#pragma once
#include "protocol.h"
#include "calibration.h"
#include <array>
#include <cstdint>
#include <deque>
#include <string>
// Collect suggestions only; applying them edits the local VE map. The normal
// verified command-32 path sends them. Persistence is always a separate action.
// No ImGui dependency: the engine is shared by live sampling, log replay and tests.
enum class FeedbackMode { Auto, WidebandOpenLoop, WidebandTrimCompensated, NarrowbandTrim };
const char* FeedbackModeName(FeedbackMode mode);

enum class AutoTuneReject {
    Calibration, EngineState, CutOrLimit, Inhibit, SensorMode,
    RpmRange, LoadRange, TpsRange, Coolant, IntakeAir, Enrichment, Idle,
    Transient, Settling, Delay, AfrImplausible, ErrorTooLarge,
    TrimActive, TrimInactive, TargetOutsideWindow, TrimAtLimit, BandUnavailable,
    PartialDwell, Outlier, LowWeight, Count
};
const char* AutoTuneRejectName(AutoTuneReject reason);

// Calibration facts the error model needs. Built from the active ECU image.
struct AutoTuneContext {
    bool calibrationMatches=false;
    int oxygenInput=-1;                 // 0x600: 0 narrowband, 1 wideband
    bool alphaN=false;                  // 0x5D4 bit 0: load axis is TPS
    float windowMinAfr=0,windowMaxAfr=0; // 0x8C8/0x8C9 narrowband target window
    float trimLeanLimit=0,trimRichLimit=0; // 0x731/0x732 as trim percent (+lean adds fuel)
    static AutoTuneContext FromCalibration(const unsigned char* cal,bool matches);
};

struct AutoTuneSettings {
    FeedbackMode mode=FeedbackMode::Auto;
    // Lambda transport delay d = delayBase + delayCycles * 120 / rpm.
    float delayBase=0.05f, delayCycles=6;
    float rpmMin=600, rpmMax=7000, loadMin=0, loadMax=255, tpsMin=0, tpsMax=100;
    float cltMin=70, iatMax=60;
    float tpsRateMax=2, rpmRateMax=400, mapRateMax=20, settleTime=0.5f;
    bool excludeIdle=false, allowWarmup=false, allowAfterstart=false, allowAccel=false;
    float afrMin=10, afrMax=20, maxErrorPercent=20, minCellWeight=0.01f;
    int minSwitches=4; float trimLimitMargin=1;
    int minHits=20; float minWeight=10, maxStdDevPercent=3;
    bool outlierRejection=true;
    // Proposal limits (do not affect collected statistics).
    float resistance=0.5f, authorityPercent=20, maxStepPercent=0;
    int veMin=1, veMax=255; float deadbandPercent=0.5f;
    bool smoothing=false;
    bool advanced=false, autoApply=false; float autoApplyInterval=10;

    enum class Preset { Conservative, Normal, Aggressive };
    void applyPreset(Preset preset);
    // True when the change invalidates collected samples (mode, delay or filters).
    bool sameErrorModel(const AutoTuneSettings& other) const;
    std::string serialize() const;
    void parse(const std::string& text);
};

struct AutoTuneCellStats {
    double sumW=0, sumWK=0, sumWK2=0;
    unsigned hits=0; float lastHit=0;
    double mean() const { return sumW>0 ? sumWK/sumW : 1; }
    double stdDev() const;
};

struct AutoTuneCellProposal {
    bool ready=false, locked=false, deadband=false, atAuthority=false, blockedByResolution=false;
    double correction=0;   // fraction applied after resistance and step limit
    double exact=0;        // unrounded proposed VE
    int baseline=0, current=0, proposed=0;
};

struct AutoTune {
    bool collecting=false;
    AutoTuneSettings settings;
    struct Frame { float time; MonitorData data; bool ok; };
    std::array<AutoTuneCellStats,256> cells{};
    std::array<bool,256> locks{};
    std::array<unsigned char,256> baseline{};
    bool baselineValid=false;
    std::array<unsigned,size_t(AutoTuneReject::Count)> rejects{};
    unsigned accepted=0;
    AutoTuneReject lastReject=AutoTuneReject::Count; // Count: last sample accepted
    std::deque<Frame> history;
    FeedbackMode activeMode=FeedbackMode::Auto;
    const char* status="Collection is off";

    void clear();                      // statistics, counters and history
    void resetHistory();               // after a gap in monitor frames
    void resetCells(const unsigned char* oldVe,const unsigned char* newVe); // changed cells and 8 neighbours
    void setBaseline(const unsigned char* ve) { std::copy(ve,ve+256,baseline.begin()); baselineValid=true; }
    FeedbackMode resolveMode(const MonitorData& m,const AutoTuneContext& ctx) const;
    float delayFor(float rpm) const;
    bool cellReady(int i) const;
    void sample(const MonitorData& m,float time,const AutoTuneContext& ctx);
    int propose(const CalBuffer& source,CalBuffer& result,std::array<AutoTuneCellProposal,256>* detail=nullptr) const;

private:
    struct Dwell { int base=-1; int lastSide=0, switches=0; double sumDt=0, sumTrim=0; std::array<double,4> w{}; std::array<unsigned,4> hits{}; };
    Dwell dwell;
    float lastFrameTime=-1, lastTransient=-1e9f, okSince=-1;
    MonitorData lastFrame;
    bool reject(AutoTuneReject reason,const char* text);
    void add(int cell,double k,double w,unsigned hits,float time);
    bool outlier(int cell,double k) const;
};
