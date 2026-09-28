#include "protocol_codec.h"
#include "protocol.h"
#include <cstring>

int BuildRequestPacket(const unsigned char* payload, int payloadLen,
                       unsigned char* packet, int packetCapacity) {
    if (!payload || !packet || payloadLen < 1 || payloadLen > 255 || packetCapacity < payloadLen + 3) return -1;
    packet[0] = REQ_HEADER;
    packet[1] = static_cast<unsigned char>(payloadLen);
    unsigned char checksum = packet[1];
    for (int i = 0; i < payloadLen; ++i) {
        packet[2 + i] = payload[i];
        checksum = static_cast<unsigned char>(checksum + payload[i]);
    }
    packet[2 + payloadLen] = checksum;
    return payloadLen + 3;
}

int ParseResponsePacket(const unsigned char* packet, int packetLen,
                        unsigned char* payload, int payloadCapacity) {
    if (!packet || packetLen < 3 || packet[0] != RESP_HEADER) return -1;
    const int payloadLen = packet[1];
    if (packetLen < payloadLen + 3 || payloadCapacity < payloadLen || (payloadLen && !payload)) return -1;
    unsigned char checksum = packet[1];
    for (int i = 0; i < payloadLen; ++i)
        checksum = static_cast<unsigned char>(checksum + packet[2 + i]);
    if (packet[2 + payloadLen] != checksum) return -1;
    if (payloadLen) std::memcpy(payload, packet + 2, payloadLen);
    return payloadLen;
}

