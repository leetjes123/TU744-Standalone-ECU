#pragma once

int BuildRequestPacket(const unsigned char* payload, int payloadLen,
                       unsigned char* packet, int packetCapacity);
int ParseResponsePacket(const unsigned char* packet, int packetLen,
                        unsigned char* payload, int payloadCapacity);

