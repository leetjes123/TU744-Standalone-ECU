#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <functional>
#include <stdexcept>
#include <vector>
#include "dtc_table.hpp"
#include "../include/identity.h"

namespace tu5jp {
using Bytes = std::vector<uint8_t>;
using Progress = std::function<void(unsigned,unsigned)>;
inline unsigned word(const Bytes& b, size_t at) { return unsigned(b.at(at))*256 + b.at(at+1); }
inline void validate_monitor(const Bytes& b) {
    if (b.size()!=98 || b[80]!=TU744_MONITOR_EXTENSION)
        throw std::runtime_error("TU744 logging requires monitor extension 4; update firmware, plugin and ADX together");
}
inline Bytes frame(const Bytes& payload, uint8_t lead=0xaa) {
    if (payload.empty() || payload.size()>128) throw std::runtime_error("Invalid packet length");
    Bytes out{lead, uint8_t(payload.size())};
    unsigned sum=unsigned(payload.size());
    for (auto b:payload) { out.push_back(b); sum+=b; }
    out.push_back(uint8_t(sum));
    return out;
}
// Returns false only for an incomplete reply. A physical adapter may echo the
// whole request. Consume it before searching for a response marker within data.
inline bool reply(const Bytes& received, const Bytes& request, Bytes& payload) {
    size_t start=0;
    if (received.empty()) return false;
    if (received[0]==0xaa) {
        size_t n=std::min(received.size(),request.size());
        if (!std::equal(received.begin(),received.begin()+n,request.begin()))
            throw std::runtime_error("Corrupt K-line echo");
        if (received.size()<request.size()) return false;
        start=request.size();
    }
    if (received.size()<start+2) return false;
    if (received[start]!=0x55 || !received[start+1] || received[start+1]>128)
        throw std::runtime_error("Invalid ECU response framing");
    size_t end=start+size_t(received[start+1])+3;
    if (received.size()<end) return false;
    if (received.size()!=end) throw std::runtime_error("Unexpected trailing ECU response");
    unsigned sum=0;
    for (size_t i=start+1;i<end-1;i++) sum+=received[i];
    if (uint8_t(sum)!=received[end-1]) throw std::runtime_error("ECU response checksum failed");
    payload.assign(received.begin()+start+2,received.begin()+end-1);
    return true;
}

inline std::string timeout_detail(const std::string& port,const Bytes& request,const Bytes& received) {
    char header[160];
    const char* state=received.empty()?"no bytes received":
        received==request?"adapter echo only":
        received[0]==0xaa && received.size()<request.size()?"partial adapter echo":"incomplete ECU reply";
    snprintf(header,sizeof(header),"ECU response timed out on %s, command 0x%02X: %s. RX (%u):",
             port.c_str(),unsigned(request.at(2)),state,unsigned(received.size()));
    std::string out=header;
    for(size_t i=0;i<std::min(received.size(),size_t(24));++i) {
        char byte[4];snprintf(byte,sizeof(byte)," %02X",unsigned(received[i]));out+=byte;
    }
    if(received.empty())out+=" none";
    if(received.size()>24)out+=" ...";
    const auto command=request.at(2);
    if(command==5 || (command>=0x21 && command<=0x24) || command==0x30)
        out+=" Write/save outcome may be unknown; read back before retrying.";
    return out;
}

class Client {
    std::function<Bytes(const Bytes&)> exchange;
    void accepted(const Bytes& b) {
        if (exchange(b)!=Bytes{0}) throw std::runtime_error("ECU rejected the operation; read status and ECU tune");
    }
    Bytes read_unchecked(unsigned at, unsigned count, const Progress& progress={}) {
        Bytes out;
        const unsigned total=count;
        while (count) {
            unsigned n=std::min(count,128u);
            auto b=exchange({4,uint8_t(at>>8),uint8_t(at),uint8_t(n)});
            if (b.size()!=n) throw std::runtime_error("Short calibration read");
            out.insert(out.end(),b.begin(),b.end()); at+=n; count-=n;
            if(progress)progress(total-count,total);
        }
        return out;
    }
public:
    explicit Client(std::function<Bytes(const Bytes&)> callback):exchange(std::move(callback)) {}
    struct Tps { unsigned raw, closed, open, generation; };
    Tps tps() {
        auto b=exchange({0x31});
        if(b.size()!=12 || b[0]!=1)
            throw std::runtime_error("TPS calibration requires TU744 firmware 0.0.1 or later");
        if(!(b[1]&4))throw std::runtime_error("Switch the ignition on before calibrating TPS");
        if(!(b[1]&2))throw std::runtime_error("Stop the engine before calibrating TPS");
        if(!(b[1]&8))throw std::runtime_error("Load a valid tune before calibrating TPS");
        if(!(b[1]&1) || word(b,2)>1023 || word(b,4)>100)
            throw std::runtime_error("No fresh TPS ADC reading; check the connection and retry");
        if(b[1]&16)throw std::runtime_error("Wait for idle-valve homing to finish, then retry");
        return {word(b,2),word(b,6),word(b,8),word(b,10)};
    }
    void apply_tps(unsigned closed,unsigned open,unsigned generation,const std::function<void()>& writing={}) {
        if(closed>923 || open>1023 || open<closed+100)
            throw std::runtime_error("TPS open must be at least 100 ADC counts above closed (0-1023)");
        if(tps().generation!=generation)
            throw std::runtime_error("ECU tune changed; read TPS and capture both endpoints again");
        const std::atomic<bool> cancelled{false};
        if(writing)writing();
        write(0x7b0,{uint8_t(closed>>8),uint8_t(closed),uint8_t(open>>8),uint8_t(open)},cancelled);
    }
    static void bounds(unsigned at,unsigned count) {
        if (at>3072 || count>3072-at) throw std::runtime_error("Use a 3072-byte schema-5 calibration BIN");
    }
    unsigned capabilities() {
        auto b=exchange({0x20});
        if (b.size()!=10 || b[0]!=3 || b[1]!=5 || word(b,2)!=3072 || b[6]!=32 || b[7]!=128)
            throw std::runtime_error("This plugin requires TU744 protocol 3 / calibration schema 5");
        return word(b,4);
    }
    void identify() {
        capabilities();
        auto b=exchange({0x2d});
        if (b!=Bytes{1,1,4,0,2,1,0,0})
            throw std::runtime_error("Stock-95080 firmware with flash calibration is required");
    }
    Bytes read(unsigned at,unsigned count,const Progress& progress={}) {
        bounds(at,count);
        if(progress)progress(0,count+1);
        unsigned generation=capabilities();
        auto out=read_unchecked(at,count,[&](unsigned done,unsigned){if(progress)progress(done,count+1);});
        if (capabilities()!=generation) throw std::runtime_error("Tune changed during read; repeat ECU download");
        if(progress)progress(count+1,count+1);
        return out;
    }
    void write(unsigned at,const Bytes& data,const std::atomic<bool>& cancelled,const Progress& progress={}) {
        bounds(at,unsigned(data.size()));
        if (data.empty()) return;
        if (at==0 && data.size()==3072 &&
            !std::equal(data.begin()+0x900,data.begin()+0x904,Bytes{'L','R',0,5}.begin()))
            throw std::runtime_error("Calibration BIN has the wrong schema marker");
        const unsigned size=unsigned(data.size()),total=2*size+1;
        if(progress)progress(0,total);
        unsigned generation=capabilities();
        accepted({0x21});
        try {
            for (size_t pos=0;pos<data.size();pos+=32) {
                if (cancelled) throw std::runtime_error("RAM update cancelled");
                size_t n=std::min(size_t(32),data.size()-pos);
                unsigned address=at+unsigned(pos);
                Bytes payload{5,uint8_t(address>>8),uint8_t(address),uint8_t(n)};
                payload.insert(payload.end(),data.begin()+pos,data.begin()+pos+n);
                accepted(payload);
                if(progress)progress(unsigned(pos+n),total);
            }
            if (cancelled) throw std::runtime_error("RAM update cancelled");
            accepted({0x22});
        } catch (...) {
            // Abort cannot undo an already accepted commit. Never repeat a
            // commit after an ambiguous response; the caller must read back.
            try { accepted({0x23}); } catch (...) {}
            throw;
        }
        if (capabilities()!=((generation+1)&65535) ||
            read_unchecked(at,size,[&](unsigned done,unsigned){if(progress)progress(size+done,total);})!=data)
            throw std::runtime_error("RAM activation not verified; download the ECU tune before further edits");
        if(progress)progress(total,total);
    }
    void begin_save() { identify(); accepted({0x24}); }
    unsigned save_status() {
        auto b=exchange({0x25});
        if (b.size()!=34) throw std::runtime_error("Invalid save status");
        return b[8];
    }
    // Stored native DTC records (command 29), 24 bytes each; see PROTOCOL.md.
    std::vector<Bytes> dtc_records() {
        auto life=exchange({0x27});
        if (life.size()!=24 || life[0]!=1) throw std::runtime_error("Unexpected diagnostic status");
        if (!life[7]) throw std::runtime_error("ECU diagnostics have not started; switch the key on and retry");
        if (life[20]>20) throw std::runtime_error("Corrupt DTC record count");
        std::vector<Bytes> out;
        for (uint8_t slot=0;slot<life[20];slot++) {
            auto r=exchange({0x29,slot});
            if (r.size()!=24) throw std::runtime_error("Unexpected DTC record");
            out.push_back(r);
        }
        return out;
    }
    void clear_dtcs() {
        if (exchange({0x30})!=Bytes{0})
            throw std::runtime_error("ECU refused the clear. Stop the engine and leave the key on. "
                                     "Firmware before 24 September 2026 cannot clear DTCs.");
    }
};

inline std::string dtc_name(uint8_t event,unsigned subtype) {
    static const unsigned subtypes[4]={1,2,4,8};
    for (const auto& e:dtc_events)
        for (int i=0;i<4;i++)
            if (e.event==event && subtypes[i]==subtype) return std::string(e.code[i])+" "+e.text[i];
    char text[32]; snprintf(text,sizeof(text),"Event 0x%02X subtype %u",event,subtype);
    return text;
}
// One entry per record; lines end in CRLF for a Win32 multiline edit.
inline std::string describe_dtcs(const std::vector<Bytes>& records) {
    if (records.empty()) return "No stored DTCs.";
    static const char* modes[3]={"stopped","cranking","running"};
    std::string out;
    for (const auto& r:records) {
        unsigned descriptor=r[2]|r[3]<<8, status=r[4]|r[5]<<8;
        unsigned latest=descriptor>>8&15, first=descriptor>>12&15;
        char line[256];
        out+=dtc_name(r[0],latest);
        snprintf(line,sizeof(line),"  [%s], MIL %s, %u occurrence%s\r\n",
                 descriptor&1?"ACTIVE":"stored",status&16?"flashing":status&8?"on":"off",
                 unsigned(r[22]),r[22]==1?"":"s");
        out+=line;
        if (first!=latest) out+="    first: "+dtc_name(r[0],first)+"\r\n";
        snprintf(line,sizeof(line),"    freeze frame: %u rpm, %u kPa, TPS %.1f%%, coolant %d C, "
                 "intake %d C, %.1f V, %u km/h, %s",unsigned(r[14])*32,unsigned(r[12]),r[11]*0.4,
                 int(r[9])-40,int(r[10])-40,r[13]/10.0,unsigned(r[17]),r[18]<3?modes[r[18]]:"?");
        out+=line;
        if (r[19]!=255) {snprintf(line,sizeof(line),", trim %.1f%%",r[19]*100.0/128);out+=line;}
        out+="\r\n";
    }
    return out;
}
}
