#pragma once
#include "protocol.h"
#include <array>
#include <string>
#include <vector>

struct DtcLabel { const char* code; const char* description; };
DtcLabel LookupDtc(unsigned event, unsigned subtype);
struct DtcRecord {
    std::array<unsigned char,24> raw{};
    unsigned event=0, subtype=0, firstSubtype=0, occurrences=0, clock=0;
    bool active=false, mil=false, flashing=false;
    int rpm=0, map=0, coolant=0, intake=0, speed=0, mode=0;
    float throttle=0, battery=0, trim=0;
    bool trimAvailable=false;
    std::string code, description, firstCode;
};
bool DecodeDtc(const unsigned char*,int,DtcRecord&);
struct ControllerFault {
    unsigned id=0, reason=0, occurrences=0;
    uint32_t first=0,last=0;
    bool active=false;
};
const char* ControllerFaultName(unsigned id);
struct DiagnosticSnapshot {
    bool valid=false, mil=false, flashing=false, dirty=false, clearPending=false, clearDurable=false;
    unsigned journalResult=0;
    uint64_t capturedAt=0;
    std::vector<DtcRecord> records;
    std::vector<ControllerFault> controller;
};
// One bounded serial exchange per step; App owns the transport while busy.
// Publish only a complete checked scan, retaining older results on failure.
struct Diagnostics {
    enum class Phase { Idle, CheckStopped, Clear, Wait, Life, Rows, VerifyLife, VerifyRows,
                       ControllerSummary, ControllerRows, VerifyController, Complete, Failed };
    enum class ClearResult { None, Accepted, Unknown, Rejected };
    Phase phase=Phase::Idle;
    ClearResult clearResult=ClearResult::None;
    DiagnosticSnapshot snapshot;
    std::string message="Connect and read fault memory to begin.";
    bool busy() const { return phase!=Phase::Idle && phase!=Phase::Complete && phase!=Phase::Failed; }
    void start(bool clear, uint64_t now);
    void step(EcuProtocol&,uint64_t now);
    float progress() const;
private:
    DiagnosticSnapshot pending;
    unsigned count=0,slot=0,retries=0,controllerMask=0,activeMask=0;
    uint64_t deadline=0,waitUntil=0;
    void fail(const char*);
    void retry();
};
