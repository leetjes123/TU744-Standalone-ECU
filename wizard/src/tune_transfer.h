#pragma once
#include "calibration_safety.h"
#include "protocol.h"
#include <array>

// Called once per UI update with monitoring stopped. A failed operation always
// invalidates the host baseline: an ACK can be lost after a mutation succeeded.
class TuneTransfer {
public:
    enum class Phase { Idle, Reading, Begin, Writing, Commit, Verify, Saving, Complete, Failed };
    Phase phase=Phase::Idle;
    std::array<unsigned char,CAL_SIZE> image{};
    std::string message;
    bool busy() const { return phase!=Phase::Idle && phase!=Phase::Complete && phase!=Phase::Failed; }
    bool read(EcuProtocol& ecu,bool allowUncalibrated=false);
    bool write(EcuProtocol& ecu,const CalBuffer& cal,const unsigned char* baseline,bool synced);
    bool save(EcuProtocol& ecu);
    void step(EcuProtocol& ecu);
    float progress() const;
    uint16_t verifiedGeneration() const { return generation; }
private:
    std::vector<CalibrationRange> ranges;
    bool live=false, transaction=false, reading=false, allowUncalibrated=false;
    int pos=0, range=0;
    uint16_t generation=0;
    DWORD started=0;
    void fail(EcuProtocol& ecu,const char* reason);
};
