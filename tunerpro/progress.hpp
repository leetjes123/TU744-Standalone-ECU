#pragma once
#include <functional>
#include <string>
#include "ITPPlugin.h"
#include "protocol.hpp"

namespace tu5jp {
// Keep the last result available even if TunerPro replaces its status text.
class TransferProgress {
    ITPProgress* host;
    std::function<void(const std::string&)> report;
    std::string operation;
    int position=-1;
    void text(const std::string& value) {
        if(host)host->SetText(const_cast<char*>(value.c_str()),UINT(value.size()+1));
        if(report)report(value);
    }
public:
    TransferProgress(ITPProgress* p,const char* name,std::function<void(const std::string&)> output):
        host(p && p->Lock()==S_OK?p:nullptr),report(std::move(output)),operation(name) {
        if(host)host->SetRange(0,100);
        update(0,1);
    }
    ~TransferProgress() {if(host)host->Unlock();}
    TransferProgress(const TransferProgress&)=delete;
    TransferProgress& operator=(const TransferProgress&)=delete;
    void update(unsigned done,unsigned total) {
        // Reserve 100% for the caller's complete validation (including verify).
        int value=total?int(99ull*done/total):0;
        if(value==position)return;
        position=value;
        if(host)host->SetCurrentPosition(value);
        text(operation+": "+std::to_string(value)+"%");
    }
    Progress callback() {return [this](unsigned done,unsigned total){update(done,total);};}
    void complete(const std::string& result) {
        if(host)host->SetCurrentPosition(100);
        text(operation+" complete: "+result);
    }
    void fail(const char* error) {text(operation+" failed: "+error);}
};
}
