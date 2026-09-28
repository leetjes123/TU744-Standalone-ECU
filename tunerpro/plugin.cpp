#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include "ITPPlugin.h"
#include "protocol.hpp"
#include "progress.hpp"

using tu5jp::Bytes;
static HINSTANCE module;
static const GUID plugin_id={0x265a1534,0xe71f,0x4b6f,{0x89,0x32,0x71,0x93,0x61,0x21,0xd0,0x01}};
static const GUID emu_id={0x265a1534,0xe71f,0x4b6f,{0x89,0x32,0x71,0x93,0x61,0x21,0xd0,0x02}};
static const GUID daq_id={0x265a1534,0xe71f,0x4b6f,{0x89,0x32,0x71,0x93,0x61,0x21,0xd0,0x03}};
static constexpr const char* registry="Software\\TU5JPStandalone\\TunerPro";
static constexpr UINT IDC_PORT=1001;
static constexpr UINT IDC_SAVE=1002;
static constexpr UINT IDC_RESULT=1003;
static constexpr UINT IDC_APPLY=1004;
static constexpr UINT IDC_STATUS=1005;
static constexpr UINT IDC_TRANSFER=1006;
static constexpr UINT IDC_READ_DTC=1007;
static constexpr UINT IDC_CLEAR_DTC=1008;
static constexpr UINT IDC_DTCS=1009;
static constexpr UINT IDC_TPS_READ=1010, IDC_TPS_CLOSED_CAPTURE=1011, IDC_TPS_OPEN_CAPTURE=1012;
static constexpr UINT IDC_TPS_APPLY=1013, IDC_TPS_RAW=1014, IDC_TPS_CLOSED=1015, IDC_TPS_OPEN=1016;
static bool tps_ready=false;
static unsigned tps_generation=0;
static HWND control_window=nullptr;
static std::mutex transfer_mutex;
static std::string transfer_result="No BIN transfer attempted in this session.";
static void report_transfer(const std::string& value) {
    std::lock_guard<std::mutex> lock(transfer_mutex);transfer_result=value;
}
static void refresh_transfer(HWND window) {
    std::lock_guard<std::mutex> lock(transfer_mutex);
    SetDlgItemTextA(window,IDC_TRANSFER,transfer_result.c_str());
}

struct Session {
    std::mutex mutex;
    HANDLE port=INVALID_HANDLE_VALUE;
    unsigned references=0;
    bool tps_download_required=false;
    std::string com;
    tu5jp::Client client{[this](const Bytes& b){return command(b);}};
    Session() {
        char value[64]{}; DWORD n=sizeof(value);
        if (RegGetValueA(HKEY_CURRENT_USER,registry,"Port",RRF_RT_REG_SZ,nullptr,value,&n)==ERROR_SUCCESS)
            com=value;
    }
    ~Session() { close(); }
    void close() { if(port!=INVALID_HANDLE_VALUE) CloseHandle(port); port=INVALID_HANDLE_VALUE; }
    static bool valid_port(const std::string& value) {
        if(value.size()<4 || value.size()>8 || value.compare(0,3,"COM")) return false;
        return value[3]!='0' && value.find_first_not_of("0123456789",3)==std::string::npos;
    }
    void acquire() {
        if(port==INVALID_HANDLE_VALUE) {
            if(!valid_port(com)) throw std::runtime_error("Configure the K-line adapter COM port first");
            port=CreateFileA(("\\\\.\\"+com).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
            if(port==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open COM port; close other tuning applications");
            try {
                DCB dcb{}; dcb.DCBlength=sizeof(dcb);
                if(!GetCommState(port,&dcb)) throw std::runtime_error("Cannot read serial settings");
                dcb.BaudRate=19200; dcb.ByteSize=8; dcb.Parity=NOPARITY; dcb.StopBits=ONESTOPBIT;
                dcb.fBinary=TRUE; dcb.fParity=FALSE; dcb.fOutxCtsFlow=FALSE; dcb.fOutxDsrFlow=FALSE;
                dcb.fDtrControl=DTR_CONTROL_DISABLE; dcb.fRtsControl=RTS_CONTROL_DISABLE;
                dcb.fOutX=dcb.fInX=FALSE; dcb.fAbortOnError=FALSE;
                // GetCommState inherits settings from earlier port users.
                // Neither DSR gating nor byte filtering belongs on this link.
                dcb.fDsrSensitivity=FALSE; dcb.fNull=FALSE; dcb.fErrorChar=FALSE;
                COMMTIMEOUTS timeout{MAXDWORD,0,10,0,1000};
                if(!SetCommState(port,&dcb) || !SetCommTimeouts(port,&timeout))
                    throw std::runtime_error("Cannot configure serial port");
                client.identify();
            } catch (...) {close(); throw;}
        }
        references++;
    }
    void release() { if(references && !--references) close(); }
    Bytes command(const Bytes& payload) {
        if(port==INVALID_HANDLE_VALUE) throw std::runtime_error("ECU is not connected");
        if(payload.empty() || payload.size()>36) throw std::runtime_error("Invalid ECU command length");
        auto request=tu5jp::frame(payload);
        if(!PurgeComm(port,PURGE_RXCLEAR)) throw std::runtime_error("Cannot clear serial receive buffer");
        DWORD sent=0;
        if(!WriteFile(port,request.data(),DWORD(request.size()),&sent,nullptr) || sent!=request.size())
            throw std::runtime_error("ECU write failed; operation outcome may be unknown");
        Bytes received, result;
        ULONGLONG deadline=GetTickCount64()+2000;
        while(GetTickCount64()<deadline) {
            uint8_t buffer[256]; DWORD count=0;
            if(!ReadFile(port,buffer,sizeof(buffer),&count,nullptr))
                throw std::runtime_error("ECU read failed; reconnect and download the ECU tune");
            received.insert(received.end(),buffer,buffer+count);
            if(received.size()>256) throw std::runtime_error("ECU response exceeds frame bound");
            if(tu5jp::reply(received,request,result)) return result;
        }
        throw std::runtime_error(tu5jp::timeout_detail(com,request,received));
    }
    void save() {
        acquire();
        try {
            client.begin_save();
            ULONGLONG deadline=GetTickCount64()+30000;
            for(;;) {
                unsigned result=0;
                // Erase temporarily masks UART interrupts. Only status reads
                // may be retried; the save command itself is sent once.
                try {result=client.save_status();}
                catch(const std::exception&) {
                    if(GetTickCount64()>=deadline)throw std::runtime_error("Save result unknown; read ECU status before another save");
                    Sleep(50); continue;
                }
                if(result==1) break;
                if(result!=0) throw std::runtime_error("ECU could not save the tune; previous valid flash tune is retained");
                if(GetTickCount64()>=deadline) throw std::runtime_error("Save result unknown; read ECU status before another save");
                Sleep(25);
            }
        } catch (...) {release(); throw;}
        release();
    }
    std::string status() {
        acquire();
        try {
            auto b=command({0x25});
            if(b.size()!=34) throw std::runtime_error("Invalid ECU status response");
            static const char* saves[]={"idle", "saved and verified", "timed out", "I/O failure"};
            unsigned result=b[8];
            std::ostringstream text;
            auto identity=command({0});
            text << std::string(identity.begin(),identity.end()) << " - tune generation " << tu5jp::word(b,4)
                 << ", flash save " << (result<4?saves[result]:"unknown")
                 << ", inhibits 0x";
            text.setf(std::ios::hex,std::ios::basefield); text.width(4); text.fill('0');
            text << tu5jp::word(b,0);
            if(b[9]) text << ". Restart required before starting.";
            auto out=text.str(); release(); return out;
        } catch (...) {release(); throw;}
    }
    std::string dtcs() {
        acquire();
        try {auto out=tu5jp::describe_dtcs(client.dtc_records()); release(); return out;}
        catch (...) {release(); throw;}
    }
    // The ECU clears on its next 10 ms diagnostic release; read back after it.
    std::string clear_dtcs() {
        acquire();
        try {
            client.clear_dtcs(); Sleep(300);
            auto out=tu5jp::describe_dtcs(client.dtc_records()); release(); return out;
        } catch (...) {release(); throw;}
    }
} session;

static void apply_port(HWND window) {
    char value[64]{}; GetDlgItemTextA(window,IDC_PORT,value,sizeof(value));
    if(!Session::valid_port(value)) throw std::runtime_error("Enter a port such as COM3");
    if(session.references && session.com!=value)
        throw std::runtime_error("Disconnect emulation and logging before changing ports");
    session.com=value;
    if(RegSetKeyValueA(HKEY_CURRENT_USER,registry,"Port",REG_SZ,value,DWORD(strlen(value)+1))!=ERROR_SUCCESS)
        throw std::runtime_error("Cannot store the COM port setting");
}
static INT_PTR CALLBACK config_proc(HWND window,UINT message,WPARAM wparam,LPARAM) {
    if(message==WM_INITDIALOG) {
        std::lock_guard<std::mutex> lock(session.mutex);
        control_window=window;
        tps_ready=false;
        EnableWindow(GetDlgItem(window,IDC_TPS_APPLY),FALSE);
        SetDlgItemTextA(window,IDC_PORT,session.com.c_str());
        EnableWindow(GetDlgItem(window,IDC_PORT),session.references==0);
        refresh_transfer(window);SetTimer(window,1,250,nullptr);
        return TRUE;
    }
    if(message==WM_TIMER && wparam==1) {refresh_transfer(window);return TRUE;}
    if(message==WM_COMMAND && LOWORD(wparam)>=IDC_TPS_READ && LOWORD(wparam)<=IDC_TPS_APPLY) {
        try {
            std::lock_guard<std::mutex> lock(session.mutex);
            char port[64]{};GetDlgItemTextA(window,IDC_PORT,port,sizeof(port));
            if(session.com!=port)tps_ready=false;
            apply_port(window);
            session.acquire();
            try {
                if(LOWORD(wparam)==IDC_TPS_APPLY) {
                    if(!tps_ready)throw std::runtime_error("Read TPS and capture the endpoints first");
                    BOOL closed_ok=FALSE,open_ok=FALSE;
                    unsigned closed=GetDlgItemInt(window,IDC_TPS_CLOSED,&closed_ok,FALSE);
                    unsigned open=GetDlgItemInt(window,IDC_TPS_OPEN,&open_ok,FALSE);
                    if(!closed_ok || !open_ok)throw std::runtime_error("Enter whole ADC counts for both endpoints");
                    session.client.apply_tps(closed,open,tps_generation,[&]{
                        session.tps_download_required=true; /* Also cover an ambiguous write result. */
                    });
                    tps_ready=false;
                    SetDlgItemTextA(window,IDC_RESULT,"TPS applied to ECU RAM and verified. Download BIN from Emulator before further edits; save the BIN and use Save ECU tune to flash to retain it.");
                } else {
                    auto value=session.client.tps();
                    if(LOWORD(wparam)==IDC_TPS_READ || !tps_ready) {
                        SetDlgItemInt(window,IDC_TPS_CLOSED,value.closed,FALSE);
                        SetDlgItemInt(window,IDC_TPS_OPEN,value.open,FALSE);
                        tps_generation=value.generation;tps_ready=true;
                    }
                    if(value.generation!=tps_generation) {
                        tps_ready=false;
                        throw std::runtime_error("Tune changed; read TPS and capture both endpoints again");
                    }
                    SetDlgItemInt(window,IDC_TPS_RAW,value.raw,FALSE);
                    if(LOWORD(wparam)==IDC_TPS_CLOSED_CAPTURE)
                        SetDlgItemInt(window,IDC_TPS_CLOSED,value.raw,FALSE);
                    if(LOWORD(wparam)==IDC_TPS_OPEN_CAPTURE)
                        SetDlgItemInt(window,IDC_TPS_OPEN,value.raw,FALSE);
                    SetDlgItemTextA(window,IDC_RESULT,"Endpoint values are staged here. Capture with the pedal released, then fully pressed; release the pedal and click Apply TPS.");
                }
            } catch(...) {session.release();throw;}
            session.release();
        } catch(const std::exception& e) {SetDlgItemTextA(window,IDC_RESULT,e.what());}
        EnableWindow(GetDlgItem(window,IDC_TPS_APPLY),tps_ready);
        return TRUE;
    }
    if(message==WM_COMMAND &&
       (LOWORD(wparam)==IDC_APPLY || LOWORD(wparam)==IDC_STATUS || LOWORD(wparam)==IDC_SAVE ||
        LOWORD(wparam)==IDC_READ_DTC || LOWORD(wparam)==IDC_CLEAR_DTC)) {
        // Ask before taking the session lock, so logging is not held up.
        if(LOWORD(wparam)==IDC_CLEAR_DTC &&
           MessageBoxA(window,"Clear all stored DTCs and freeze frames?\n\nStop the engine and leave the key on. "
                       "Faults that are still present will be stored again.","Clear DTCs",
                       MB_YESNO|MB_ICONQUESTION)!=IDYES)
            return TRUE;
        try {
            std::lock_guard<std::mutex> lock(session.mutex);
            apply_port(window);
            if(LOWORD(wparam)==IDC_READ_DTC) {
                SetDlgItemTextA(window,IDC_DTCS,session.dtcs().c_str());
                SetDlgItemTextA(window,IDC_RESULT,"DTCs read from the ECU.");
            } else if(LOWORD(wparam)==IDC_CLEAR_DTC) {
                SetDlgItemTextA(window,IDC_DTCS,session.clear_dtcs().c_str());
                SetDlgItemTextA(window,IDC_RESULT,"DTCs cleared. The list below was read back afterwards; "
                                "faults that are still present are stored again.");
            } else if(LOWORD(wparam)==IDC_SAVE) {
                SetDlgItemTextA(window,IDC_RESULT,"Saving; keep the engine stopped...");
                session.save();
                SetDlgItemTextA(window,IDC_RESULT,"Saved and verified. Reset the ECU before starting.");
            } else if(LOWORD(wparam)==IDC_STATUS)
                SetDlgItemTextA(window,IDC_RESULT,session.status().c_str());
            else SetDlgItemTextA(window,IDC_RESULT,"COM port setting saved.");
        } catch(const std::exception& e) { SetDlgItemTextA(window,IDC_RESULT,e.what()); }
        return TRUE;
    }
    if((message==WM_COMMAND && LOWORD(wparam)==IDCANCEL) || message==WM_CLOSE) {
        DestroyWindow(window); return TRUE;
    }
    if(message==WM_DESTROY) {KillTimer(window,1);if(control_window==window)control_window=nullptr;return TRUE;}
    return FALSE;
}
static HRESULT configure(TPP_CONFIGINFO* info) {
    if(!info || info->cbSize<sizeof(*info)) return E_INVALIDARG;
    if(control_window) {
        ShowWindow(control_window,SW_SHOWNORMAL); SetForegroundWindow(control_window); return S_OK;
    }
    HWND window=CreateDialogParamA(module,MAKEINTRESOURCEA(101),info->hwndOwner,config_proc,0);
    if(!window)return E_FAIL;
    ShowWindow(window,SW_SHOWNORMAL); return S_OK;
}
static BOOL component_info(TPP_COMPONENTINFO* info,bool emulation) {
    if(!info || info->cbSize<sizeof(*info)) return FALSE;
    info->ID=emulation?emu_id:daq_id;
    strcpy_s(info->strName,emulation?"TU744 schema-4 RAM tuning":"TU744 schema-4 logging");
    strcpy_s(info->strDesc,"Shared K-line connection, stock 95080 / flash calibration");
    strcpy_s(info->strVersion,"0.5.0");
    info->bConfigurable=TRUE; info->wVersionMajor=0; info->wVersionMinor=5;
    return TRUE;
}
struct Errors {
    std::string error;
    template<class F> HRESULT run(F f) {
        try { f(); error.clear(); return S_OK; }
        catch(const std::exception& e) { error=e.what(); return E_FAIL; }
        catch(...) { error="Unexpected plugin failure"; return E_FAIL; }
    }
    const char* last(BOOL& prompt) { prompt=!error.empty(); return error.c_str(); }
};
class Emulator final:public ITPEmulator,private Errors {
    bool attached=false;
    std::atomic<bool> cancelled{false};
    void require() { if(!attached) throw std::runtime_error("Connect emulation first"); }
    template<class F> HRESULT transfer(ITPProgress* host,const char* name,F f) {
        return run([&]{
            tu5jp::TransferProgress progress(host,name,report_transfer);
            try {
                std::lock_guard<std::mutex> lock(session.mutex);require();
                progress.complete(f(progress.callback()));
            } catch(const std::exception& e) {progress.fail(e.what());throw;}
            catch(...) {progress.fail("Unexpected plugin failure");throw;}
        });
    }
public:
    ~Emulator() override {ReleaseHardware();}
    TPPCOMPONENT_TYPE GetType() override {return TPPLUGINCOMPONENT_EMULATION_DRIVER;}
    BOOL GetComponentInfo(TPP_COMPONENTINFO* p) override {return component_info(p,true);}
    HRESULT Configure(TPP_CONFIGINFO* p) override {return configure(p);}
    LONG_PTR MessageHandler(UINT,LPARAM,LPARAM) override {return 0;}
    const CHAR* GetLastErrorText(BOOL& p) override {return last(p);}
    HRESULT GetHardwareInfo(TPEMUCAPS* p) override {
        if(!p || p->cbSize<sizeof(*p)) return E_INVALIDARG;
        strcpy_s(p->strName,"TU744"); strcpy_s(p->strDescription,"3072-byte schema-4 RAM calibration");
        strcpy_s(p->strVersion,"0.5.0");
        // TunerPro gates its entire emulation toolbar on CHIPEMULATION.
        // Our chip-sized buffer is the ECU's RAM calibration; REALTIME alone
        // detects successfully but leaves download and enable disabled.
        p->dwCapFlags=TPP_EMU_CAP_CHIPEMULATION|TPP_EMU_CAP_REALTIMEMULATION;
        p->uiTotalMemorySize=3072; p->uiBankCount=1; p->uiAlignmentBits=0;
        return S_OK;
    }
    HRESULT InitializeHardware() override {return run([&]{std::lock_guard<std::mutex> lock(session.mutex); if(!attached){session.acquire();attached=true;}});}
    HRESULT ReleaseHardware() override {return run([&]{std::lock_guard<std::mutex> lock(session.mutex); if(attached){session.release();attached=false;}});}
    HRESULT CancelOperation() override {cancelled=true;return S_OK;}
    HRESULT GetBank(UINT* p) override {if(!p)return E_INVALIDARG;*p=0;return S_OK;}
    HRESULT SetBank(UINT bank) override {return bank?E_INVALIDARG:S_OK;}
    HRESULT GetCurrentBankSize(UINT* p) override {if(!p)return E_INVALIDARG;*p=3072;return S_OK;}
    HRESULT GetJustificationOffset(UINT n,UINT* p) override {if(!p||n>3072)return E_INVALIDARG;*p=0;return S_OK;}
    HRESULT WriteData(TPEMUWRITE* p) override {
        if(!p||p->cbSize<sizeof(*p)||(!p->pData&&p->cbData)||p->dwFlags) return E_INVALIDARG;
        if(p->puiTransferred)*p->puiTransferred=0;
        return transfer(p->pIProgress,"Upload",[&](const tu5jp::Progress& progress){
            cancelled=false;
            if(p->cbData && session.tps_download_required)
                throw std::runtime_error("TPS was calibrated in the panel; Download BIN from Emulator before uploading or editing");
            if(p->cbData)session.client.write(p->dwAddress,Bytes(p->pData,p->pData+p->cbData),cancelled,progress);
            if(p->puiTransferred)*p->puiTransferred=p->cbData;
            return std::to_string(p->cbData)+" bytes; "+(p->cbData?"RAM activation and readback verified":"no data requested");
        });
    }
    HRESULT ReadData(TPEMUREAD* p) override {
        if(!p||p->cbSize<sizeof(*p)||(!p->pBuffer&&p->cbReadSize)||p->dwFlags)return E_INVALIDARG;
        if(p->puiTransferred)*p->puiTransferred=0;
        return transfer(p->pIProgress,"Download",[&](const tu5jp::Progress& progress){
            auto b=session.client.read(p->dwAddress,p->cbReadSize,progress);
            if(!b.empty())memcpy(p->pBuffer,b.data(),b.size());
            if(p->puiTransferred)*p->puiTransferred=UINT(b.size());
            if(p->dwAddress==0 && b.size()==3072)session.tps_download_required=false;
            return std::to_string(b.size())+" bytes received from ECU RAM";});
    }
    HRESULT VerifyData(TPEMUVERIFY* p) override {
        if(!p||p->cbSize<sizeof(*p)||(!p->pDataToCompare&&p->cbData)||p->dwFlags)return E_INVALIDARG;
        bool mismatch=false;
        HRESULT result=transfer(p->pIProgress,"Verify",[&](const tu5jp::Progress& progress){
            auto b=session.client.read(p->dwAddress,p->cbData,progress);
            for(size_t i=0;i<b.size();++i)if(b[i]!=p->pDataToCompare[i]) {
                mismatch=true;
                char detail[128];
                sprintf_s(detail,"ECU tune differs at 0x%04X: ECU %02X, BIN %02X",
                          p->dwAddress+unsigned(i),unsigned(b[i]),unsigned(p->pDataToCompare[i]));
                throw std::runtime_error(detail);
            }
            return std::to_string(b.size())+" ECU RAM bytes match the open BIN";});
        // SDK VerifyData is BOOL-valued despite its HRESULT declaration:
        // 1=match, 0=mismatch, negative=I/O failure. S_OK would mean mismatch.
        return mismatch?FALSE:(FAILED(result)?result:TRUE);
    }
    HRESULT BeginTrace(TPTRACEINIT*) override {return E_NOTIMPL;}
    BOOL IsTracing() override {return FALSE;}
    HRESULT EndTrace() override {return S_OK;}
};
class Acquisition final:public ITPDataAcqIO,private Errors {
    bool attached=false;
    Bytes queued;
    TPDATAACQTIMEOUTS timeout{sizeof(TPDATAACQTIMEOUTS),0,0,2000,0,2000,0};
public:
    ~Acquisition() override {ReleaseHardware();}
    TPPCOMPONENT_TYPE GetType() override {return TPPLUGINCOMPONENT_DATAACQIO_DRIVER;}
    BOOL GetComponentInfo(TPP_COMPONENTINFO* p) override {return component_info(p,false);}
    HRESULT Configure(TPP_CONFIGINFO* p) override {return configure(p);}
    LONG_PTR MessageHandler(UINT,LPARAM,LPARAM) override {return 0;}
    const CHAR* GetLastErrorText(BOOL& p) override {return last(p);}
    HRESULT GetHardwareInfo(TPDATAACQIOCAPS* p) override {
        if(!p||p->cbSize<sizeof(*p))return E_INVALIDARG;
        strcpy_s(p->strName,"TU744 shared K-line"); strcpy_s(p->strDescription,"Validated frames; physical adapter echo removed");
        strcpy_s(p->strVersion,"0.5.0");p->dwCapFlags=0;return S_OK;
    }
    HRESULT InitializeHardware(TPDATAACQHWINIT* p) override {
        if(!p||p->cbSize<sizeof(*p)||p->dwBaud!=19200||p->btParity||p->btBitsPerByte!=8||p->btStopBits)return E_INVALIDARG;
        return run([&]{std::lock_guard<std::mutex> lock(session.mutex);if(!attached){session.acquire();attached=true;}});
    }
    HRESULT ReleaseHardware() override {return run([&]{std::lock_guard<std::mutex> lock(session.mutex);queued.clear();if(attached){session.release();attached=false;}});}
    HRESULT WriteData(TPDATAACQWRITE* p) override {
        if(!p||p->cbSize<sizeof(*p)||!p->pData||p->cbData!=4||p->dwFlags)return E_INVALIDARG;
        if(p->puiTransferred)*p->puiTransferred=0;
        return run([&]{std::lock_guard<std::mutex> lock(session.mutex);
            if(!attached)throw std::runtime_error("Connect logging first");
            uint8_t command=p->pData[2];
            if((command!=0x10&&command!=0x25&&command!=0x2b) ||
               Bytes(p->pData,p->pData+4)!=tu5jp::frame({command}))
                throw std::runtime_error("Use the TU744 schema-4 ADX; logging cannot write calibration");
            auto response=session.command({command});
            if(command==0x10)tu5jp::validate_monitor(response);
            queued=tu5jp::frame(response,0x55);
            if(p->puiTransferred)*p->puiTransferred=4;});
    }
    HRESULT ReadData(TPDATAACQREAD* p) override {
        if(!p||p->cbSize<sizeof(*p)||(!p->pBuffer&&p->cbReadSize)||p->dwFlags)return E_INVALIDARG;
        if(p->puiTransferred)*p->puiTransferred=0;
        return run([&]{std::lock_guard<std::mutex> lock(session.mutex);
            if(!attached)throw std::runtime_error("Connect logging first");
            size_t count=std::min(size_t(p->cbReadSize),queued.size());
            if(count)memcpy(p->pBuffer,queued.data(),count);
            queued.erase(queued.begin(),queued.begin()+count);
            if(p->puiTransferred)*p->puiTransferred=UINT(count);});
    }
    HRESULT SetTimeouts(TPDATAACQTIMEOUTS* p) override {if(!p||p->cbSize<sizeof(*p))return E_INVALIDARG;timeout=*p;return S_OK;}
    HRESULT GetTimeouts(TPDATAACQTIMEOUTS* p) override {if(!p||p->cbSize<sizeof(*p))return E_INVALIDARG;*p=timeout;return S_OK;}
    HRESULT PurgeTransmitBuffer() override {return S_OK;}
    HRESULT PurgeReceiveBuffer() override {std::lock_guard<std::mutex> lock(session.mutex);queued.clear();return S_OK;}
};
class Plugin final:public ITPPlugin {
public:
    ~Plugin() override {if(control_window)DestroyWindow(control_window);}
    BOOL GetPluginInfo(TPP_PLUGININFO* p) override {
        if(!p||p->cbSize<sizeof(*p))return FALSE;
        p->dwContractVersion=TPPLUGIN_CONTRACT_VERSION;p->ID=plugin_id;
        strcpy_s(p->strName,"TU744 schema 4");
        strcpy_s(p->strDesc,"RAM tuning, shared K-line logging and explicit flash save for stock 95080 firmware");
        strcpy_s(p->strVersion,"0.5.0");strcpy_s(p->strAuthor,"TU744 project");
        p->dwComponentCount=2;p->bConfigurable=TRUE;p->wVersionMajor=0;p->wVersionMinor=5;return TRUE;
    }
    HRESULT Configure(TPP_CONFIGINFO* p) override {return configure(p);}
    ITPPluginComponent* GetComponent(UINT index) override {
        try {if(index==0)return new Emulator;if(index==1)return new Acquisition;}catch(...){}
        return nullptr;
    }
    VOID ReleaseComponent(ITPPluginComponent* p) override {delete p;}
    LONG_PTR MessageHandler(UINT,LPARAM,LPARAM) override {return 0;}
};
extern "C" __declspec(dllexport) ITPPlugin* TPCreatePlugin() {try{return new Plugin;}catch(...){return nullptr;}}
extern "C" __declspec(dllexport) void TPReleasePlugin(ITPPlugin* p) {delete p;}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {if(reason==DLL_PROCESS_ATTACH)module=instance;return TRUE;}
