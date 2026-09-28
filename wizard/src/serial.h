#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct SerialPort {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::string portName;
    bool connected = false;

    bool open(const char* port, int baudRate = 19200);
    void close();
    int  read(unsigned char* buf, int maxLen, int timeoutMs = 100);
    bool write(const unsigned char* buf, int len);
    bool isOpen() const { return handle != INVALID_HANDLE_VALUE && connected; }

    static std::vector<std::string> enumerate();
};
