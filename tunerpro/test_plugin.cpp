#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <iostream>
#include "ITPPlugin.h"
#include "protocol.hpp"
extern "C" {
#include "ecu.h"
#include "oem_runtime.h"
void bridge_reset(void);
u16 bridge_exchange(const u8*,u16,u8*);
// No physical flash is touched by these protocol tests.
u8 hal_cal_read(u8,u16,u8*,u8) {return 0;}
u8 hal_cal_erase(u8) {return 0;}
u8 hal_cal_program(u8,u16,const u8*,u8) {return 0;}
}
#include "test_host.hpp"
using tu5jp::Bytes;
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static HWND find_controls() {
    HWND found=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM context)->BOOL {
        char title[128]{};GetWindowTextA(window,title,sizeof(title));
        if(!strcmp(title,"TU744 controls")) {
            *reinterpret_cast<HWND*>(context)=window;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&found));
    return found;
}
int main() {
    unsigned commits=0;
    auto exchange=[&](const Bytes& payload) {
        auto request=tu5jp::frame(payload);
        uint8_t raw[256]{};
        if(payload[0]==0x22)commits++;
        auto n=bridge_exchange(request.data(),u16(request.size()),raw);
        Bytes received(raw,raw+n), result;
        CHECK(tu5jp::reply(received,request,result));
        return result;
    };
    tu5jp::Client client(exchange);
    bridge_reset(); client.identify();
    CHECK(exchange({0})==Bytes({'T','U','7','4','4',' ','0','.','0','.','1'}));
    auto original=client.read(0,3072);
    CHECK(original.size()==3072 && original[0x903]==4);
    std::atomic<bool> cancel{false};
    auto next=original;next[0]=88;
    client.write(0,next,cancel);
    CHECK(client.read(0,3072)==next && commits==1);
    // Partial updates are staged, validated, committed and read back.
    client.write(1,{77,66},cancel);
    CHECK(client.read(0,3)==Bytes({88,77,66}));
    unsigned generation=client.capabilities();
    try{client.write(0x550,{0,0},cancel);CHECK(false);}catch(const std::runtime_error&){}
    CHECK(client.capabilities()==generation && client.read(0x550,2)==Bytes({9,186}));
    cancel=true;
    auto count=commits;
    try{client.write(0,next,cancel);CHECK(false);}catch(const std::runtime_error&){}
    CHECK(commits==count && !ecu.cal.staging);
    cancel=false;
    auto old=next;old[0x903]=3;
    try{client.write(0,old,cancel);CHECK(false);}catch(const std::runtime_error&){}
    CHECK(commits==count);
    try{client.write(3071,{1,2},cancel);CHECK(false);}catch(const std::runtime_error&){}
    ecu.control.mode=ENGINE_RUNNING;
    try{client.write(0x605,{0,60},cancel);CHECK(false);}catch(const std::runtime_error&){}
    CHECK(client.read(0x605,2)==Bytes({0,0}));
    ecu.control.mode=ENGINE_STOPPED;
    // Lost commit response: report uncertainty and abort staging, never repeat.
    count=commits;
    tu5jp::Client ambiguous([&](const Bytes& p){auto out=exchange(p);if(p[0]==0x22)throw std::runtime_error("lost reply");return out;});
    try{ambiguous.write(0,{81},cancel);CHECK(false);}catch(const std::runtime_error&){}
    CHECK(commits==count+1 && client.read(0,1)==Bytes({81}));
    CHECK(!ecu.cal.staging);
    // Framing split at every byte, with and without physical echo. Include
    // response-marker bytes in the echo so they cannot be mistaken for reply.
    auto request=tu5jp::frame({5,0,0,3,0x55,0xaa,0x55});
    auto response=tu5jp::frame(Bytes(98,0x55),0x55);
    auto caps_request=tu5jp::frame({0x20});
    CHECK(tu5jp::timeout_detail("COM3",caps_request,{}).find("command 0x20: no bytes received")!=std::string::npos);
    CHECK(tu5jp::timeout_detail("COM3",caps_request,caps_request).find("adapter echo only. RX (4): AA 01 20 21")!=std::string::npos);
    CHECK(tu5jp::timeout_detail("COM3",caps_request,{0xaa}).find("partial adapter echo")!=std::string::npos);
    CHECK(tu5jp::timeout_detail("COM3",caps_request,{0x55,10,3}).find("incomplete ECU reply")!=std::string::npos);
    CHECK(tu5jp::timeout_detail("COM3",tu5jp::frame({0x24}),{}).find("outcome may be unknown")!=std::string::npos);
    for(int echo=0;echo<2;echo++) {
        Bytes wire=echo?request:Bytes{};wire.insert(wire.end(),response.begin(),response.end());
        for(size_t n=0;n<=wire.size();n++) {
            Bytes result;
            CHECK(tu5jp::reply(Bytes(wire.begin(),wire.begin()+n),request,result)==(n==wire.size()));
            if(n==wire.size())CHECK(result==Bytes(98,0x55));
        }
        wire.back()^=1;
        try{Bytes result;tu5jp::reply(wire,request,result);CHECK(false);}catch(const std::runtime_error&){}
    }
    // Full-range temperatures survive the actual C parser and signed BE decode.
    for (int temperature : {-40,0,127,128,140,150}) {
        ecu.sensors.clt.value=ecu.sensors.iat.value=static_cast<s16>(temperature);
        auto monitor=exchange({0x10});tu5jp::validate_monitor(monitor);
        auto signed_word=[](const Bytes& b,size_t at){unsigned v=tu5jp::word(b,at);return v&32768?int(v)-65536:int(v);};
        CHECK(signed_word(monitor,82)==temperature && signed_word(monitor,84)==temperature);
    }
    try{tu5jp::validate_monitor(Bytes(98,0));CHECK(false);}catch(const std::runtime_error&){}
    {Bytes extension1(98,0);extension1[80]=1;  // Lacks inhibits/DTC counts.
     try{tu5jp::validate_monitor(extension1);CHECK(false);}catch(const std::runtime_error&){}}
    {Bytes extension2(98,0);extension2[80]=2;  // Lacks narrowband state.
     try{tu5jp::validate_monitor(extension2);CHECK(false);}catch(const std::runtime_error&){}}
    // Read the real C monitor: inclusive transition band, mode/quality/age
    // gating and operation even while STFT is disabled and the engine stopped.
    bridge_reset();
    auto* cal=ecu.cal.bytes[ecu.cal.active];
    cal[0x600]=0;cal[0x8c3]=80;cal[0x8c4]=100;
    ecu.sensors.oxygen.quality=QUALITY_VALID;
    ecu.milliseconds=ecu.sensors.stamp=100;
    ecu.control.trim_enabled=0;ecu.control.rich=1;
    for(const auto& sample : std::vector<std::pair<unsigned,unsigned>>{{399,1},{400,2},{450,2},{500,2},{501,4}}) {
        ecu.sensors.oxygen_mv=static_cast<u16>(sample.first);
        auto monitor=exchange({0x10});tu5jp::validate_monitor(monitor);
        CHECK(monitor[93]==sample.second && ecu.control.rich==1 && !ecu.control.trim_enabled);
    }
    cal[0x600]=1;CHECK(exchange({0x10})[93]==0);cal[0x600]=0;
    for(unsigned quality : {QUALITY_RANGE,QUALITY_STALE,QUALITY_CONFIG}) {
        ecu.sensors.oxygen.quality=static_cast<u8>(quality);CHECK(exchange({0x10})[93]==0);
    }
    ecu.sensors.oxygen.quality=QUALITY_VALID;
    ecu.milliseconds=100+get16(cal+CAL_SENSOR_AGE)+1;CHECK(exchange({0x10})[93]==0);
    ecu.milliseconds=100;ecu.cal.valid=0;CHECK(exchange({0x10})[93]==0);ecu.cal.valid=1;
    ecu.sensors.tps.quality=ecu.sensors.map.quality=ecu.sensors.clt.quality=QUALITY_RANGE;
    ecu.sensors.iat.quality=ecu.sensors.oxygen.quality=ecu.sensors.battery.quality=QUALITY_STALE;
    auto status=exchange({0x25});CHECK(status.size()==34);
    CHECK(std::all_of(status.begin()+20,status.begin()+26,[](u8 value){return value==0;}));
    // Load the actual Win32 SDK plugin and exercise its exported ABI and
    // disconnected-call behavior. No COM port is opened by this harness.
    HMODULE dll=LoadLibraryA("TU744.dll");CHECK(dll!=nullptr);
    auto create=reinterpret_cast<ITPPlugin*(*)()>(GetProcAddress(dll,"TPCreatePlugin"));
    auto release=reinterpret_cast<void(*)(ITPPlugin*)>(GetProcAddress(dll,"TPReleasePlugin"));
    CHECK(create && release);
    auto plugin=create();CHECK(plugin!=nullptr);
    TPP_PLUGININFO info{};info.cbSize=sizeof(info);CHECK(plugin->GetPluginInfo(&info));
    CHECK(info.dwComponentCount==2 && info.dwContractVersion==TPPLUGIN_CONTRACT_VERSION);
    // Configuration is a persistent modeless control panel. Reopening focuses
    // the same panel rather than creating a second window or blocking TunerPro.
    TPP_CONFIGINFO config{};config.cbSize=sizeof(config);
    CHECK(SUCCEEDED(plugin->Configure(&config)));
    HWND controls=find_controls();CHECK(controls!=nullptr);
    CHECK(SUCCEEDED(plugin->Configure(&config)));
    CHECK(find_controls()==controls);
    auto emu=static_cast<ITPEmulator*>(plugin->GetComponent(0));CHECK(emu!=nullptr);
    TPEMUCAPS caps{};caps.cbSize=sizeof(caps);CHECK(SUCCEEDED(emu->GetHardwareInfo(&caps)));
    CHECK(caps.uiTotalMemorySize==3072);
    // The host checks CHIPEMULATION before enabling download/verify/live edit.
    CHECK((caps.dwCapFlags&TPP_EMU_CAP_CHIPEMULATION)!=0);
    CHECK((caps.dwCapFlags&TPP_EMU_CAP_REALTIMEMULATION)!=0);
    CHECK(!(caps.dwCapFlags&(TPP_EMU_CAP_WRITEONLY|TPP_EMU_CAP_READONLY|TPP_EMU_CAP_NOVERIFY)));
    TPP_COMPONENTINFO component{};component.cbSize=sizeof(component);
    CHECK(emu->GetComponentInfo(&component));
    CHECK(component.wVersionMajor==info.wVersionMajor && component.wVersionMinor==info.wVersionMinor);
    CHECK(!strcmp(component.strVersion,info.strVersion) && !strcmp(caps.strVersion,info.strVersion));
    UINT transferred=999;uint8_t byte=77;
    TPEMUWRITE write{sizeof(write),&byte,1,0,&transferred,0,nullptr};
    CHECK(FAILED(emu->WriteData(&write)) && !transferred);
    auto daq=static_cast<ITPDataAcqIO*>(plugin->GetComponent(1));CHECK(daq!=nullptr);
    CHECK(daq->GetComponentInfo(&component));
    CHECK(component.wVersionMajor==0 && component.wVersionMinor>=3);
    CHECK(!strcmp(component.strVersion,info.strVersion));
    TPDATAACQREAD read{sizeof(read),&byte,1,&transferred,0};
    CHECK(FAILED(daq->ReadData(&read)) && !transferred);
    CHECK(plugin->GetComponent(2)==nullptr);
    // Exercise the actual DLL with Win32 serial calls redirected in this test
    // process only. The real C firmware parser is the ECU; no COM port or
    // registry write reaches the OS. This catches host-specific return values
    // that standalone Client tests cannot detect.
    CHECK(testhost::install(dll));
    SetDlgItemTextA(controls,1001,"COM77");
    SendMessageA(controls,WM_COMMAND,1004,0);
    bridge_reset();
    CHECK(emu->InitializeHardware()==S_OK);
    CHECK(!testhost::active_dcb.fNull && !testhost::active_dcb.fDsrSensitivity && !testhost::active_dcb.fErrorChar);
    testhost::Progress download_progress;
    Bytes remote(3072);
    TPEMUREAD download{sizeof(download),remote.data(),UINT(remote.size()),0,&transferred,0,&download_progress};
    CHECK(emu->ReadData(&download)==S_OK && transferred==3072);
    CHECK(download_progress.position==100 && download_progress.positions.size()>10);
    CHECK(download_progress.locks==1 && download_progress.unlocks==1 && !download_progress.locked);
    CHECK(std::is_sorted(download_progress.positions.begin(),download_progress.positions.end()));
    testhost::Progress verify_progress;
    TPEMUVERIFY verify{sizeof(verify),remote.data(),UINT(remote.size()),0,0,&verify_progress};
    CHECK(emu->VerifyData(&verify)==TRUE); // S_OK (0) means mismatch to TunerPro!
    CHECK(verify_progress.position==100 && verify_progress.text.find("match")!=std::string::npos);
    remote[0]^=1;
    verify_progress.positions.clear();
    CHECK(emu->VerifyData(&verify)==FALSE);
    CHECK(verify_progress.position<100 && verify_progress.text.find("0x0000")!=std::string::npos);
    SendMessageA(controls,WM_TIMER,1,0);
    char result[256]{};GetDlgItemTextA(controls,1006,result,sizeof(result));
    CHECK(std::string(result).find("Verify failed: ECU tune differs")!=std::string::npos);
    testhost::Progress upload_progress;
    TPEMUWRITE upload{sizeof(upload),remote.data(),UINT(remote.size()),0,&transferred,0,&upload_progress};
    CHECK(emu->WriteData(&upload)==S_OK && transferred==3072);
    CHECK(upload_progress.position==100 && upload_progress.positions.size()>50);
    CHECK(std::is_sorted(upload_progress.positions.begin(),upload_progress.positions.end()));
    CHECK(upload_progress.text.find("RAM activation and readback verified")!=std::string::npos);
    CHECK(emu->VerifyData(&verify)==TRUE);
    testhost::fail_read=true;
    CHECK(FAILED(emu->VerifyData(&verify)) && verify_progress.position<100);
    CHECK(verify_progress.text.find("ECU read failed")!=std::string::npos);
    testhost::fail_read=false;
    testhost::Progress busy_progress;busy_progress.available=false;
    verify.pIProgress=&busy_progress;
    CHECK(emu->VerifyData(&verify)==TRUE);
    CHECK(busy_progress.locks==1 && busy_progress.unlocks==0 && busy_progress.positions.empty());
    // A rejected upload must not display 100% or claim completion.
    unsigned saved=remote[0x903];remote[0x903]=3;
    CHECK(FAILED(emu->WriteData(&upload)) && transferred==0 && upload_progress.position<100);
    CHECK(upload_progress.text.find("wrong schema marker")!=std::string::npos);
    remote[0x903]=uint8_t(saved);
    CHECK(emu->VerifyData(&verify)==TRUE);
    TPDATAACQHWINIT init{sizeof(init),19200,NOPARITY,8,ONESTOPBIT,FALSE};
    CHECK(daq->InitializeHardware(&init)==S_OK);
    for(unsigned cycle=0;cycle<5;++cycle)for(unsigned command:{0x10u,0x25u,0x2bu}) {
        auto poll_request=tu5jp::frame({uint8_t(command)});
        TPDATAACQWRITE send{sizeof(send),poll_request.data(),DWORD(poll_request.size()),&transferred,0};
        CHECK(daq->WriteData(&send)==S_OK && transferred==poll_request.size());
        Bytes packet((command==0x10?98:command==0x25?34:12)+3);
        TPDATAACQREAD poll_response{sizeof(poll_response),packet.data(),DWORD(packet.size()),&transferred,0};
        CHECK(daq->ReadData(&poll_response)==S_OK && transferred==packet.size());
        Bytes payload;CHECK(tu5jp::reply(packet,poll_request,payload));
        if(command==0x10)tu5jp::validate_monitor(payload);
        if(command==0x25)CHECK(tu5jp::word(payload,4)==ecu.cal.generation && ecu.cal.generation>0);
    }
    CHECK(SUCCEEDED(emu->ReleaseHardware()));
    CHECK(SUCCEEDED(daq->ReleaseHardware()));
    // DTC panel: stored records with freeze frames (29) and a confirmed clear (30).
    char dtcs[2048]{},panel[512]{};
    SendMessageA(controls,WM_COMMAND,1007,0);
    GetDlgItemTextA(controls,1003,panel,sizeof(panel));
    CHECK(strstr(panel,"diagnostics have not started")!=nullptr);  // history not booted
    oem_runtime.booted=oem_runtime.initialized=1;
    oem_runtime.history.ready=1;
    oem_runtime.state.events.gate=32;
    oem_runtime.state.events.clear_inverse=0xFFFF;
    ecu.key_input=1;
    const u8 record[24]={0x1B,0,0x01,0x41,0x48,0,0,0,0,130,65,50,98,138,94,65,65,72,2,136,0x01,0x2C,3,0};
    memcpy(oem_runtime.state.events.store.records[0],record,24);
    oem_runtime.state.events.store.count=1;
    SendMessageA(controls,WM_COMMAND,1007,0);
    GetDlgItemTextA(controls,1009,dtcs,sizeof(dtcs));
    CHECK(strstr(dtcs,"P0123 Throttle position circuit high  [ACTIVE], MIL on, 3 occurrences")!=nullptr);
    CHECK(strstr(dtcs,"first: P0120 Throttle position sensor circuit")!=nullptr);
    CHECK(strstr(dtcs,"3008 rpm, 98 kPa, TPS 20.0%, coolant 90 C, intake 25 C, 13.8 V, 72 km/h, running, trim 106.2%")!=nullptr);
    testhost::confirm_answer=IDNO;
    SendMessageA(controls,WM_COMMAND,1008,0);
    CHECK(testhost::confirmations==1 && !oem_runtime.state.events.clear_request);
    testhost::confirm_answer=IDYES;
    ecu.control.mode=ENGINE_RUNNING;
    SendMessageA(controls,WM_COMMAND,1008,0);
    GetDlgItemTextA(controls,1003,panel,sizeof(panel));
    CHECK(strstr(panel,"ECU refused the clear")!=nullptr && !oem_runtime.state.events.clear_request);
    ecu.control.mode=ENGINE_STOPPED;
    SendMessageA(controls,WM_COMMAND,1008,0);
    GetDlgItemTextA(controls,1003,panel,sizeof(panel));
    // Admitted; the runtime (not run by this harness) services it next release.
    CHECK(strstr(panel,"DTCs cleared")!=nullptr && oem_runtime.state.events.clear_request==106);
    CHECK(testhost::confirmations==3);
    oem_runtime.state.events.store.count=0;
    SendMessageA(controls,WM_COMMAND,1007,0);
    GetDlgItemTextA(controls,1009,dtcs,sizeof(dtcs));
    CHECK(!strcmp(dtcs,"No stored DTCs."));
    CHECK(tu5jp::dtc_name(0x7F,0)=="Event 0x7F subtype 0");
    // Real DLL panel and actual ECU parser: capture is read-only, apply is one
    // atomic four-byte transaction, and a stale host BIN cannot undo it.
    ecu.control.mode=ENGINE_STOPPED;ecu.rotation.state=ROT_UNSYNCED;ecu.rotation.rpm=0;
    ecu.iac.state=IAC_READY;ecu.key_input=1;
    ecu.adc[8].seen=1;ecu.adc[8].stamp=ecu.milliseconds;ecu.adc[8].raw=187;
    auto endpoints=client.read(0x7b0,4);
    SendMessageA(controls,WM_COMMAND,1011,0);
    CHECK(GetDlgItemInt(controls,1015,nullptr,FALSE)==187);
    ecu.adc[8].raw=887;ecu.adc[8].stamp=ecu.milliseconds;
    SendMessageA(controls,WM_COMMAND,1012,0);
    CHECK(GetDlgItemInt(controls,1016,nullptr,FALSE)==887);
    CHECK(client.read(0x7b0,4)==endpoints);
    ecu.adc[8].raw=187;ecu.adc[8].stamp=ecu.milliseconds;
    SendMessageA(controls,WM_COMMAND,1013,0);
    CHECK(client.read(0x7b0,4)==Bytes({0,187,3,119}));
    GetDlgItemTextA(controls,1003,panel,sizeof(panel));
    CHECK(strstr(panel,"verified")!=nullptr && strstr(panel,"Download BIN")!=nullptr);
    CHECK(SUCCEEDED(emu->InitializeHardware()));
    CHECK(FAILED(emu->WriteData(&upload))); // old PC BIN must not undo calibration
    remote.resize(3072);
    TPEMUREAD tps_download{sizeof(tps_download),remote.data(),3072,0,&transferred,0,nullptr};
    CHECK(SUCCEEDED(emu->ReadData(&tps_download)) && remote[0x7b1]==187 && remote[0x7b3]==119);
    upload.pData=remote.data();upload.cbData=3072;
    CHECK(SUCCEEDED(emu->WriteData(&upload)));
    CHECK(SUCCEEDED(emu->ReleaseHardware()));
    auto tps_generation=client.capabilities();
    auto reject_tps=[&](){try{client.tps();CHECK(false);}catch(const std::runtime_error&){};};
    ecu.adc[8].seen=0;reject_tps();ecu.adc[8].seen=1;
    ecu.adc[8].stamp=ecu.milliseconds-101;reject_tps();ecu.adc[8].stamp=ecu.milliseconds;
    ecu.adc[8].raw=1024;reject_tps();ecu.adc[8].raw=187;
    ecu.control.mode=ENGINE_RUNNING;reject_tps();ecu.control.mode=ENGINE_STOPPED;
    ecu.rotation.rpm=1;reject_tps();ecu.rotation.rpm=0;
    ecu.key_input=0;reject_tps();ecu.key_input=1;
    ecu.iac.state=IAC_HOMING;reject_tps();ecu.iac.state=IAC_READY;
    for(auto pair:std::vector<std::pair<unsigned,unsigned>>{{187,286},{900,800},{0,1024}}) {
        try{client.apply_tps(pair.first,pair.second,tps_generation);CHECK(false);}catch(const std::runtime_error&){}
    }
    CHECK(client.capabilities()==tps_generation && client.read(0x7b0,4)==Bytes({0,187,3,119}));
    try{client.apply_tps(200,900,tps_generation+1);CHECK(false);}catch(const std::runtime_error&){}
    CHECK(client.capabilities()==tps_generation);
    SendMessageA(controls,WM_CLOSE,0,0);CHECK(!IsWindow(controls));
    CHECK(SUCCEEDED(plugin->Configure(&config)));
    controls=find_controls();CHECK(controls!=nullptr);
    plugin->ReleaseComponent(daq);plugin->ReleaseComponent(emu);release(plugin);
    CHECK(!IsWindow(controls));FreeLibrary(dll);
    std::cout<<"PASS "<<checks<<" C++/actual-C-parser and Win32 SDK DLL assertions; no physical serial I/O\n";
}
