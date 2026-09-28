#include "serial.h"
#include <cstdio>

bool SerialPort::open(const char* port, int baudRate) {
    close();

    // Windows needs \\.\ prefix for COM ports above COM9
    std::string fullPort = std::string("\\\\.\\") + port;
    handle = CreateFileA(fullPort.c_str(), GENERIC_READ | GENERIC_WRITE,
                         0, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) return false;

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) { close(); return false; }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    dcb.fBinary  = TRUE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;

    if (!SetCommState(handle, &dcb)) { close(); return false; }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout         = 10;
    timeouts.ReadTotalTimeoutMultiplier  = 1;
    timeouts.ReadTotalTimeoutConstant    = 100;
    timeouts.WriteTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant   = 100;
    SetCommTimeouts(handle, &timeouts);

    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    portName = port;
    connected = true;
    return true;
}

void SerialPort::close() {
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
    connected = false;
    portName.clear();
}

int SerialPort::read(unsigned char* buf, int maxLen, int timeoutMs) {
    if (!isOpen()) return -1;

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout         = 10;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = timeoutMs;
    timeouts.WriteTotalTimeoutConstant   = 1000;
    SetCommTimeouts(handle, &timeouts);

    DWORD bytesRead = 0;
    if (!ReadFile(handle, buf, maxLen, &bytesRead, NULL)) return -1;
    return (int)bytesRead;
}

bool SerialPort::write(const unsigned char* buf, int len) {
    if (!isOpen()) return false;
    DWORD bytesWritten = 0;
    if (!WriteFile(handle, buf, len, &bytesWritten, NULL)) return false;
    // Note: echo handling is done by EcuProtocol::transact() which reads
    // echo+response as one blob (half-duplex safe)
    return (int)bytesWritten == len;
}

std::vector<std::string> SerialPort::enumerate() {
    std::vector<std::string> ports;
    char buf[65536];
    DWORD len = QueryDosDeviceA(NULL, buf, sizeof(buf));
    if (len == 0) return ports;

    const char* p = buf;
    while (*p) {
        if (strncmp(p, "COM", 3) == 0) {
            ports.push_back(p);
        }
        p += strlen(p) + 1;
    }
    return ports;
}
