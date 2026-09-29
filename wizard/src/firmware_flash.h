#pragma once

#include "protocol.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

constexpr size_t ECU_FIRMWARE_IMAGE_SIZE = 0x80000;

struct FirmwareImageInfo {
    size_t size = 0;
    size_t programmedBytes = 0;
    size_t lastProgrammedOffset = 0;
    int sectorsToErase = 0;
};

bool LoadAndValidateFirmwareImage(const char* path, std::vector<unsigned char>& image,
                                  FirmwareImageInfo& info, std::string& error);

class FirmwareFlasher {
public:
    enum class State { Idle, Entering, Erasing, Programming, Resetting, Complete, Failed };

    FirmwareFlasher() = default;
    ~FirmwareFlasher();
    FirmwareFlasher(const FirmwareFlasher&) = delete;
    FirmwareFlasher& operator=(const FirmwareFlasher&) = delete;

    bool start(EcuProtocol* ecu, std::vector<unsigned char> image,
               const FirmwareImageInfo& info);
    void joinIfFinished();

    State state() const { return state_.load(); }
    bool busy() const;
    float progress() const { return progress_.load(); }
    std::string status() const;
    FirmwareImageInfo info() const { return info_; }

private:
    void run();
    void setStatus(const char* text);
    void fail(const char* text, bool tryReset);
    bool writeRaw(const unsigned char* data, int len);
    bool validateHandler();
    bool rawExchange(const unsigned char* command,int length,unsigned char* response,int count,int timeout=3000);

    EcuProtocol* ecu_ = nullptr;
    std::vector<unsigned char> image_;
    FirmwareImageInfo info_;
    std::thread worker_;
    std::atomic<State> state_{State::Idle};
    std::atomic<float> progress_{0.0f};
    mutable std::mutex statusMutex_;
    std::string status_ = "Idle";
};
