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
    // Host-side link health, filled in by the monitor thread (not sent by the ECU).
    uint32_t linkFailures=0;    // failed live-frame requests since connect
    uint8_t linkLastFailure=0;  // LinkFailure code of the most recent failure
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
    void setPort(SerialPort* p) { port=p; liveV4Unsupported=false; }
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
