#pragma once
// Test-process-only IAT substitutes for the built DLL. No port or registry write
// reaches Windows: framed serial traffic goes to the actual firmware C parser.
#include <cstring>
#include <string>
#include <vector>
#include "ITPPlugin.h"

namespace testhost {
inline HANDLE fake_port=reinterpret_cast<HANDLE>(0x1234);
inline std::vector<unsigned char> receive;
inline bool fail_read=false;
inline unsigned writes=0;
inline DCB active_dcb{};
inline HANDLE WINAPI open(LPCSTR name,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE) {
    if(std::strcmp(name,"\\\\.\\COM77"))return INVALID_HANDLE_VALUE;
    return fake_port;
}
inline BOOL WINAPI close(HANDLE h) {return h==fake_port;}
inline BOOL WINAPI get_state(HANDLE h,LPDCB d) {
    if(h!=fake_port)return FALSE;
    d->DCBlength=sizeof(*d);
    // A previous COM-port user can leave receive filtering enabled.
    d->fNull=TRUE;d->fDsrSensitivity=TRUE;d->fErrorChar=TRUE;
    return TRUE;
}
inline BOOL WINAPI set_state(HANDLE h,LPDCB d) {
    active_dcb=*d;
    return h==fake_port&&d->BaudRate==19200&&d->ByteSize==8;
}
inline BOOL WINAPI timeouts(HANDLE h,LPCOMMTIMEOUTS) {return h==fake_port;}
inline BOOL WINAPI purge(HANDLE h,DWORD) {receive.clear();return h==fake_port;}
inline unsigned confirmations=0;
inline int confirm_answer=IDYES;
inline int WINAPI confirm(HWND,LPCSTR,LPCSTR,UINT) {++confirmations;return confirm_answer;}
inline LSTATUS WINAPI registry(HKEY,LPCSTR,LPCSTR,DWORD,LPCVOID,DWORD) {return ERROR_SUCCESS;}
inline BOOL WINAPI write(HANDLE h,LPCVOID data,DWORD n,LPDWORD sent,LPOVERLAPPED) {
    if(h!=fake_port)return FALSE;
    ++writes;
    const auto* bytes=static_cast<const unsigned char*>(data);
    unsigned char response[256]{};
    auto count=bridge_exchange(bytes,static_cast<u16>(n),response);
    // Include KKL echo and fragment reads, covering the production transport.
    receive.assign(bytes,bytes+n);receive.insert(receive.end(),response,response+count);
    // Model the documented Windows receive behavior with DSR low.
    if(active_dcb.fNull)receive.erase(std::remove(receive.begin(),receive.end(),0),receive.end());
    if(active_dcb.fDsrSensitivity)receive.clear();
    *sent=n;return TRUE;
}
inline BOOL WINAPI read(HANDLE h,LPVOID data,DWORD n,LPDWORD got,LPOVERLAPPED) {
    *got=0;
    if(h!=fake_port||fail_read)return FALSE;
    size_t count=std::min({size_t(n),receive.size(),size_t(17)});
    if(count)std::memcpy(data,receive.data(),count);
    receive.erase(receive.begin(),receive.begin()+count);
    *got=DWORD(count);return TRUE;
}
inline bool hook(HMODULE dll,const char* name,void* replacement) {
    auto* base=reinterpret_cast<unsigned char*>(dll);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto* imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;imports->Name;++imports) {
        auto* names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->OriginalFirstThunk);
        auto* slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;
            auto* entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            if(std::strcmp(reinterpret_cast<const char*>(entry->Name),name))continue;
            DWORD previous;
            if(!VirtualProtect(&slots->u1.Function,sizeof(void*),PAGE_READWRITE,&previous))return false;
            slots->u1.Function=reinterpret_cast<ULONG_PTR>(replacement);
            DWORD unused;return !!VirtualProtect(&slots->u1.Function,sizeof(void*),previous,&unused);
        }
    }
    return false;
}
inline bool install(HMODULE dll) {
    return hook(dll,"CreateFileA",reinterpret_cast<void*>(open)) &&
        hook(dll,"CloseHandle",reinterpret_cast<void*>(close)) &&
        hook(dll,"GetCommState",reinterpret_cast<void*>(get_state)) &&
        hook(dll,"SetCommState",reinterpret_cast<void*>(set_state)) &&
        hook(dll,"SetCommTimeouts",reinterpret_cast<void*>(timeouts)) &&
        hook(dll,"PurgeComm",reinterpret_cast<void*>(purge)) &&
        hook(dll,"RegSetKeyValueA",reinterpret_cast<void*>(registry)) &&
        hook(dll,"WriteFile",reinterpret_cast<void*>(write)) &&
        hook(dll,"ReadFile",reinterpret_cast<void*>(read)) &&
        hook(dll,"MessageBoxA",reinterpret_cast<void*>(confirm));
}
class Progress final:public ITPProgress {
public:
    bool locked=false,available=true;
    int low=0,high=0,position=0;
    unsigned locks=0,unlocks=0;
    std::vector<int> positions;
    std::string text;
    TPAPPCOMPONENT_TYPE GetType() override {return TPAPPCOMPONENT_PROGRESS;}
    HRESULT Lock() override {++locks;if(!available)return S_FALSE;locked=true;return S_OK;}
    BOOL IsLocked() override {return locked;}
    HRESULT Unlock() override {++unlocks;locked=false;return S_OK;}
    BOOL SetRange(INT a,INT b) override {low=a;high=b;return TRUE;}
    BOOL GetRange(INT& a,INT& b) override {a=low;b=high;return TRUE;}
    INT SetCurrentPosition(INT n) override {int old=position;position=n;positions.push_back(n);return old;}
    INT GetCurrentPosition() override {return position;}
    INT Step() override {return SetCurrentPosition(position+1);}
    HRESULT SetText(CHAR* value,UINT) override {text=value?value:"";return S_OK;}
    HRESULT GetText(CHAR* out,UINT count) override {if(!count)return E_INVALIDARG;strncpy_s(out,count,text.c_str(),_TRUNCATE);return S_OK;}
};
}
