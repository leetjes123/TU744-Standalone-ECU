#pragma once
#include "serial.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>
#include <functional>
#include <cstdint>
#define CMD_GET_VERSION 0x00
#define CMD_FLASH_MODE 0x01
#define CMD_READ_RAM 0x04
#define CMD_WRITE_RAM 0x05
#define CMD_MONITOR 0x13
#define RESP_HEADER 0x55
#define REQ_HEADER 0xAA
#define MAX_WRITE_CHUNK 32
#define MAX_READ_CHUNK 128

struct MonitorData {
    uint16_t rpm=0, kpa=0, ve=0, pulseUs=0, lossOfSyncCount=0;
    float tps=0, battery=0, measuredAfr=0, targetAfr=0, trimPercent=0, advance=0;
    int clt=0, iat=0, oxygenMv=0, warmup=0, afterstart=0, accel=0;
    int iacPosition=0, idleTarget=0, speed=0, gear=0, narrowbandBand=0;
    int cellRpm=0, cellLoad=0;
    float rpmFraction=0, loadFraction=0;
    uint8_t state=0, flags=0;
    uint16_t inhibits=0, generation=0;
    bool valid=false;
    bool knockAvailable=false;
    uint16_t knockMv=65535;
    uint8_t knockFlags=0;
    float knockRetard=0;
    // Command 13 version 4 (firmware 0.0.3+): idle-actuator and sync-loss detail.
    bool detailAvailable=false;
    int iacTarget=0;
    uint8_t iacState=0, iacFault=0, iacLastFault=0, iacRetries=0, iacFaultCount=0, iacResponse=0;
    uint8_t idleMode=0, syncLossReason=0, syncLossTooth=0;
    float idleIntegral=0, idleSpark=0;
    uint16_t firstInhibit=0, captureResyncs=0, captureOverruns=0;
    // Command 13 version 5 (firmware 0.0.8+): output trip source and per-event output faults.
    bool outputDetailAvailable=false;
    uint8_t tripSource=0, latchClears=0;
    uint16_t coilTimeouts=0, injectorClamps=0, lateEvents=0;
    // Command 13 version 6 (firmware 0.0.8+): full-range oxygen input (oxygenMv
    // above 1275 mV), enrichment-adjusted target AFR and the knock fallback.
    bool v6Available=false;
    float adjustedTargetAfr=0;
    bool knockFallback=false;
    // Host-side link health, filled in by the monitor thread (not sent by the ECU).
    uint32_t linkFailures=0;    // failed live-frame requests since connect
    uint8_t linkLastFailure=0;  // LinkFailure code of the most recent failure
    uint32_t droppedFrames=0;   // received frames discarded by a full queue since connect
    float sampleGap=0;          // seconds since the previous logged sample (set by the logger)
    float plannedDutyPercent() const { return pulseUs * float(rpm) / 600000.0f; }
    bool synced() const { return (state&1)!=0; }
    bool unsaved() const { return (flags&16)!=0; }
};
// Why a request failed. Logged so that link noise and ECU silence can be told apart.
enum LinkFailure : uint8_t {
    LINK_OK=0, LINK_TIMEOUT=1, LINK_SERIAL_ERROR=2, LINK_ECHO_MISMATCH=3, LINK_BAD_FRAME=4, LINK_DECODE=5
};
// One live frame, stamped (QueryPerformanceCounter) when its reply was complete.
struct MonitorSample {
    MonitorData data;
    int64_t receivedQpc=0;
};
struct Capabilities { uint16_t generation=0; };
struct EcuStatus {
    uint16_t inhibits=0, generation=0, errorOffset=0;
    uint8_t saveResult=0, service=0, iacState=0;
};
struct TpsSnapshot {
    uint8_t flags=0; uint16_t raw=0, ageMs=0, closed=0, open=0, generation=0;
};
bool DecodeMonitor(const unsigned char* data, int length, MonitorData& out);
const char* IacStateName(int state);
const char* IacFaultName(int fault);
const char* IdleModeName(int mode);
const char* SyncLossReasonName(int reason);
const char* TripSourceName(int source);
// Inhibit bit 0..8 (command 13 bytes 34..35): short name, and a hover text
// with what blocks outputs and how it clears.
const char* InhibitName(int bit);
const char* InhibitDetail(int bit);
bool InhibitLatches(int bit, bool firmwareBefore008);
// Command 2F version 2: worst timing since reset and the first crank capture failure.
struct TimingHealth {
    uint16_t passMaxMs=0, intervalMaxMs=0, tickMaxMs=0, planAgeMaxMs=0, captureOverruns=0, lateEvents=0;
    uint16_t captureReason=0, counter=0, expected=0, last=0, latest=0, irq=0, pec=0, dest=0, resyncs=0;
    uint8_t head=0, tail=0, next=0, count=0;
    uint16_t words[30]={};
};
bool DecodeTimingHealth(const unsigned char* data, int length, TimingHealth& out);
const char* CaptureFailureName(int reason);
// Lost crank edges in a capture snapshot: indices i where words[i-1]..words[i]
// spans about two tooth periods (the 60-2 gap spans about three).
std::vector<int> LostCaptureEdges(const TimingHealth& health, uint16_t* normalInterval = nullptr);
// Command 34 (64 bytes) and 35 03 (86 bytes, with the bench job result): knock sensing detail.
struct KnockDetail {
    uint16_t mv=65535; uint8_t flags=0, retard=0;
    uint16_t raw=0, sequence=0, hold=0, epoch=0, generation=0;
    uint32_t stampMs=0, sampleAgeMs=0, count=0, lastEventAgeMs=0, normalCount=0, nullCount=0, testCount=0;
    uint8_t source=0, type=0, offset=0, amplitude=0, reference=0, gain=0, gainCode=0;
    uint8_t ratio=0, threshold=0, decision=0, scheduled=0, fault=0, nullStart=0;
    uint16_t adcTimeouts=0, missedWindows=0, staleSamples=0, ceilingEvents=0;
    bool job=false; uint8_t jobState=0, jobCompleted=0, jobFault=0;
    uint16_t nullRaw=0, testRaw=0, gainRaw[7]={};
};
bool DecodeKnockDetail(const unsigned char* data, int length, KnockDetail& out);
// Knock sensing fault 0..8 (command 34 byte 36): short name, and a hover text
// with what the ECU measured and what to check.
const char* KnockFaultName(int fault);
const char* KnockFaultDetail(int fault);
// Bench job state (command 35 03 byte 65).
const char* KnockJobStateName(int state);
struct EcuProtocol {
    SerialPort* port=nullptr;
    char versionString[64]={};
    MonitorData monitor={};
    std::atomic<bool> monitorActive{false};
    std::atomic<int> monitorFailCount{0};
    uint8_t lastFailure=LINK_OK; // cause of the last failed transact (serial owner only)
    // Transport injection is used by protocol and failure-recovery tests.
    std::function<int(const unsigned char*,int,unsigned char*,int,int)> exchange;
    // Set once the ECU rejects a version 4 live request (firmware 0.0.2 or older).
    bool liveV4Unsupported=false;
    // Set once the ECU rejects a version 5 live request (firmware 0.0.7 or older).
    bool liveV5Unsupported=false;
    // Set once the ECU rejects a version 6 live request (firmware without live frame 6).
    bool liveV6Unsupported=false;
    void setPort(SerialPort* p) { port=p; liveV4Unsupported=false; liveV5Unsupported=false; liveV6Unsupported=false; }
    bool timingHealth(TimingHealth&, bool clearMaxima);
    bool knockDetail(KnockDetail&, bool withJob);
    // Command 35: 0 stops a bench job, 1 starts engine-off sampling, 2 runs the self-test.
    bool knockJob(uint8_t action);
    int transact(const unsigned char*, int, unsigned char*, int, int timeoutMs=500);
    bool getVersion();
    bool capabilities(Capabilities&);
    bool status(EcuStatus&);
    bool command(unsigned char);
    bool readChunk(int offset,int len,unsigned char* out);
    bool writeChunk(int offset,int len,const unsigned char* data);
    bool liveWrite(int offset,int len,const unsigned char* data,uint16_t& generation,uint16_t& edits);
    bool tpsSnapshot(TpsSnapshot&);
    int readTpsAdc();
    bool parseMonitorResponse(const unsigned char* data,int len);
    bool readMonitor(MonitorData&);
    void pollMonitor();
    // Background monitor thread â€” polls ECU without blocking the UI
    HANDLE              monitorThread = nullptr;
    CRITICAL_SECTION    serialCS;       // protects serial port access
    std::atomic<bool>       threadRunning = false;
    std::atomic<bool>       threadPaused = false;  // pause during cal transfers
    MonitorData         threadMonitor = {};     // worker's parse buffer
    MonitorData*        monitorParseTarget = nullptr; // worker parses away from UI-owned snapshot
    int                 monitorIntervalMs = 5; // poll interval
    // Every received frame is queued for the UI. The queue has its own short
    // lock, so the UI never waits for (or misses frames during) a serial
    // transaction. Bounded in case the UI thread stalls (modal dialogs).
    static constexpr size_t MONITOR_QUEUE_MAX = 4096;
    std::mutex              sampleMutex;
    std::deque<MonitorSample> sampleQueue;
    uint32_t                linkFailureTotal = 0;   // monitor thread only
    uint8_t                 linkLastFailure = LINK_OK; // monitor thread only
    uint32_t                droppedTotal = 0;       // guarded by sampleMutex

    void initThread();      // call once at startup (inits CS)
    void startMonitor();    // start background polling
    bool stopMonitor();     // stop background polling; false if thread did not exit
    void destroyThread();   // call at shutdown
    bool consumeMonitorUpdate(MonitorData& out); // latest frame only; drops the rest
    size_t drainMonitorSamples(std::vector<MonitorSample>& out); // all frames, oldest first
    void monitorCycle();    // one poll of the monitor thread (also used by tests)
    int  getMonitorFailCount();
    void resetMonitorFailCount();
    void clearMonitorUpdates();

    // Lock serial port for main-thread operations (cal read/write)
    void lockSerial()   { threadPaused = true; EnterCriticalSection(&serialCS); }
    void unlockSerial() { LeaveCriticalSection(&serialCS); threadPaused = false; }

};
