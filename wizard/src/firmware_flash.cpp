#include "firmware_flash.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <windows.h>

namespace {

struct FlashSector {
    size_t endOffset;
    unsigned char command[4];
};

// Commands recovered from the proven legacy FlashUpdate utility. Addresses use
// the flash handler's bank + 16-bit byte-offset format.
const FlashSector FLASH_SECTORS[] = {
    {0x04000, {0x05, 0x00, 0x00, 0x00}},
    {0x06000, {0x05, 0x01, 0x00, 0x00}},
    {0x08000, {0x05, 0x01, 0x20, 0x00}},
    {0x10000, {0x05, 0x02, 0x00, 0x00}},
    {0x20000, {0x05, 0x04, 0x00, 0x00}},
    {0x30000, {0x05, 0x08, 0x00, 0x00}},
    {0x40000, {0x05, 0x0C, 0x00, 0x00}},
    {0x50000, {0x05, 0x10, 0x00, 0x00}},
    {0x60000, {0x05, 0x14, 0x00, 0x00}},
    {0x70000, {0x05, 0x18, 0x00, 0x00}},
    {0x80000, {0x05, 0x1C, 0x00, 0x00}},
};

int RequiredSectorCount(size_t lastOffset) {
    for (int i = 0; i < static_cast<int>(std::size(FLASH_SECTORS)); ++i) {
        if (lastOffset < FLASH_SECTORS[i].endOffset) return i + 1;
    }
    return static_cast<int>(std::size(FLASH_SECTORS));
}

bool IsAllFF(const unsigned char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) if (data[i] != 0xFF) return false;
    return true;
}

} // namespace

bool LoadAndValidateFirmwareImage(const char* path, std::vector<unsigned char>& image,
                                  FirmwareImageInfo& info, std::string& error) {
    image.clear();
    info = {};
    error.clear();

    FILE* file = fopen(path, "rb");
    if (!file) {
        error = "Could not open the firmware image.";
        return false;
    }
    fseek(file, 0, SEEK_END);
    const long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize != static_cast<long>(ECU_FIRMWARE_IMAGE_SIZE)) {
        fclose(file);
        error = "Firmware must be an exact 524,288-byte raw .bin image (not an H86/HEX or calibration file).";
        return false;
    }

    image.resize(ECU_FIRMWARE_IMAGE_SIZE);
    const size_t read = fread(image.data(), 1, image.size(), file);
    fclose(file);
    if (read != image.size()) {
        image.clear();
        error = "The firmware image could not be read completely.";
        return false;
    }

    // C166 reset/interrupt vectors in known Standalone images begin with a JMPS
    // opcode (FA 00). This also rejects all-FF files and 4 KiB calibration bins.
    if (image[0] != 0xFA || image[1] != 0x00) {
        image.clear();
        error = "The file does not look like an Standalone C166 firmware image (invalid reset vector).";
        return false;
    }

    size_t last = 0;
    size_t nonFF = 0;
    for (size_t i = 0; i < image.size(); ++i) {
        if (image[i] != 0xFF) {
            last = i;
            ++nonFF;
        }
    }
    if (nonFF < 1024) {
        image.clear();
        error = "The image contains too little program data to be valid firmware.";
        return false;
    }

    info.size = image.size();
    info.programmedBytes = nonFF;
    info.lastProgrammedOffset = last;
    info.sectorsToErase = RequiredSectorCount(std::max(last,size_t(0x6FFFF)));
    return true;
}

FirmwareFlasher::~FirmwareFlasher() {
    if (worker_.joinable()) worker_.join();
}

bool FirmwareFlasher::busy() const {
    const State s = state_.load();
    return s == State::Entering || s == State::Erasing ||
           s == State::Programming || s == State::Resetting;
}

std::string FirmwareFlasher::status() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

void FirmwareFlasher::setStatus(const char* text) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = text;
}

bool FirmwareFlasher::start(EcuProtocol* ecu, std::vector<unsigned char> image,
                            const FirmwareImageInfo& info) {
    if (!ecu || !ecu->port || !ecu->port->isOpen() || busy()) return false;
    joinIfFinished();
    ecu_ = ecu;
    image_ = std::move(image);
    info_ = info;
    progress_ = 0.0f;
    state_ = State::Entering;
    setStatus("Entering ECU firmware update mode...");
    worker_ = std::thread(&FirmwareFlasher::run, this);
    return true;
}

void FirmwareFlasher::joinIfFinished() {
    const State s = state_.load();
    if (worker_.joinable() && (s == State::Complete || s == State::Failed)) worker_.join();
}

bool FirmwareFlasher::writeRaw(const unsigned char* data, int len) {
    return ecu_ && ecu_->port && ecu_->port->isOpen() && ecu_->port->write(data, len);
}

bool FirmwareFlasher::rawExchange(const unsigned char* command,int length,unsigned char* response,int count,int timeout) {
    if(!writeRaw(command,length)) return false;
    unsigned char bytes[64]; int got=0; const int needed=length+count;
    const DWORD start=GetTickCount();
    while(got<needed && GetTickCount()-start<DWORD(timeout)) {
        const int n=ecu_->port->read(bytes+got,needed-got,100);
        if(n<0) return false; got+=n;
    }
    if(got!=needed || memcmp(bytes,command,length)) return false;
    if(count) memcpy(response,bytes+length,count);
    return true;
}
bool FirmwareFlasher::validateHandler() {
    const unsigned char probe=0x66; unsigned char answer=0;
    return rawExchange(&probe,1,&answer,1,10000) && answer==0xFF;
}
void FirmwareFlasher::fail(const char* text,bool) {
    // Leave the RAM handler running after any uncertain flash operation.
    // Resetting an incomplete image would remove the remaining recovery path.
    setStatus(text); state_=State::Failed;
}

void FirmwareFlasher::run() {
    ecu_->lockSerial();
    bool handlerEntered = false;
    PurgeComm(ecu_->port->handle, PURGE_RXCLEAR);

    unsigned char response[4];
    const unsigned char enterCommand = CMD_FLASH_MODE;
    const int enterResult = ecu_->transact(&enterCommand, 1, response, sizeof(response), 1200);
    if (enterResult != 1 || response[0] != 0x00) {
        ecu_->unlockSerial();
        fail("ECU refused firmware update mode. Ensure the engine is stopped.", false);
        return;
    }
    handlerEntered = true;
    Sleep(150);
    if (!validateHandler()) {
        ecu_->unlockSerial();
        fail("RAM flash handler did not answer its safety probe. Power-cycle the ECU before retrying.", true);
        return;
    }

    state_ = State::Erasing;
    for (int sector = info_.sectorsToErase - 1; sector >= 0; --sector) {
        char message[96];
        snprintf(message, sizeof(message), "Erasing firmware sector %d of %d...",
                 info_.sectorsToErase - sector, info_.sectorsToErase);
        setStatus(message);
        if (!rawExchange(FLASH_SECTORS[sector].command, 4, nullptr, 0)) {
            ecu_->unlockSerial();
            fail("Serial error while erasing ECU flash. The ECU must be recovered before use.", handlerEntered);
            return;
        }
        // The RAM handler is silent while the AMD flash erase is busy.
        if(!validateHandler()) {
            ecu_->unlockSerial(); fail("Erase completion probe failed; update is unverified",false); return;
        }
        progress_ = 0.15f * static_cast<float>(info_.sectorsToErase - sector) /
                    static_cast<float>(info_.sectorsToErase);
    }

    state_ = State::Programming;
    setStatus("Programming ECU firmware...");
    size_t sentBlocks = 0;
    for (size_t offset = 0; offset < image_.size(); offset += 8) {
        const unsigned char* source = image_.data() + offset;
        if (!IsAllFF(source, 8)) {
            unsigned char packet[12];
            const size_t bank = offset / 0x4000;
            const size_t bankOffset = offset % 0x4000;
            packet[0] = 0x02;
            packet[1] = static_cast<unsigned char>(bank);
            packet[2] = static_cast<unsigned char>(bankOffset >> 8);
            packet[3] = static_cast<unsigned char>(bankOffset);
            // The C166 handler word-writes little-endian values; swap every pair
            // so the byte-oriented .bin lands in flash unchanged.
            for (int i = 0; i < 8; i += 2) {
                packet[4 + i] = source[i + 1];
                packet[5 + i] = source[i];
            }
            if (!rawExchange(packet, sizeof(packet), nullptr, 0)) {
                ecu_->unlockSerial();
                fail("Serial error while programming ECU flash. The ECU must be recovered before use.", handlerEntered);
                return;
            }
            ++sentBlocks;
            if ((sentBlocks & 0x1F) == 0) {
                if (!FlushFileBuffers(ecu_->port->handle)) {
                    ecu_->unlockSerial();
                    fail("Serial flush failed during firmware programming.", handlerEntered);
                    return;
                }
                PurgeComm(ecu_->port->handle, PURGE_RXCLEAR);
            }
        }
        progress_ = 0.15f + 0.83f * static_cast<float>(offset + 8) /
                                static_cast<float>(image_.size());
    }
    FlushFileBuffers(ecu_->port->handle);

    // Check the handler's erase/program result before resetting.
    unsigned char statusCmd=7,statusBytes[2]={};
    // A legacy handler returns one FF byte; read that before deciding the length.
    if(!rawExchange(&statusCmd,1,statusBytes,1)) {
        ecu_->unlockSerial(); fail("Update status unavailable; firmware is unverified",false); return;
    }
    if(statusBytes[0]==0x5A) {
        if(ecu_->port->read(statusBytes+1,1,1000)!=1 || statusBytes[1]!=0) {
            ecu_->unlockSerial(); fail("Flash handler reported an erase/program failure",false); return;
        }
    } else if(statusBytes[0]!=0xFF) {
        ecu_->unlockSerial(); fail("Unexpected flash status; firmware is unverified",false); return;
    }

    state_ = State::Resetting;
    setStatus("Firmware programmed. Resetting ECU...");
    const unsigned char reset[4] = {0x06, 0x00, 0x00, 0x00};
    if (!writeRaw(reset, sizeof(reset)) || !FlushFileBuffers(ecu_->port->handle)) {
        ecu_->unlockSerial();
        fail("Firmware was sent, but the ECU reset command failed. Power-cycle the ECU.", false);
        return;
    }
    Sleep(1200);
    PurgeComm(ecu_->port->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    ecu_->unlockSerial();

    progress_ = 1.0f;
    setStatus("Firmware programmed and reset command sent.");
    state_ = State::Complete;
}
