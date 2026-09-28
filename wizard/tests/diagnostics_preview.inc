// Compiled only into the isolated visual-test target. Never shipped.
#include <cstdio>
#include <cstring>
ImVec2 diagnosticPreviewCardCenter;
static void DiagnosticPreviewInput(int frame) {
    if(!strstr(GetCommandLineA(),"--freeze")) return;
    auto& io=ImGui::GetIO();
    if(frame==3) { io.AddMousePosEvent(diagnosticPreviewCardCenter.x,diagnosticPreviewCardCenter.y); io.AddMouseButtonEvent(0,true); }
    if(frame==4) io.AddMouseButtonEvent(0,false);
}
static void InitDiagnosticPreview() {
    auto& s=gApp.diagnostics.snapshot;
    s.valid=true; s.mil=true; s.capturedAt=GetTickCount64();
    unsigned char raw[24]={}; raw[0]=0x43; raw[2]=1; raw[3]=0x22; raw[4]=8;
    raw[9]=128; raw[10]=74; raw[11]=46; raw[12]=36; raw[13]=141;
    raw[14]=94; raw[17]=62; raw[18]=2; raw[19]=131; raw[20]=3; raw[21]=14; raw[22]=3;
    DtcRecord r; DecodeDtc(raw,24,r); s.records.push_back(r);
    raw[0]=0x61; raw[2]=0; raw[3]=0x22; raw[4]=0; raw[22]=1;
    DecodeDtc(raw,24,r); s.records.push_back(r);
    raw[0]=0x27; raw[2]=1; raw[3]=0x11; raw[4]=8; raw[22]=5;
    DecodeDtc(raw,24,r); s.records.push_back(r);
    gApp.diagnostics.message="Visual test fixture / synthetic records / no ECU connected";
    gApp.openToolTab(App::OpenTab::DIAGNOSTICS);
    if(strstr(GetCommandLineA(),"--empty")) { s.records.clear(); s.mil=false; }
    if(strstr(GetCommandLineA(),"--contrast")) { gApp.uiTheme=UiTheme::HighContrast; ApplyUiTheme(gApp.uiTheme,g_dpiScale); }
}
static bool SaveDiagnosticPreview() {
    if(strstr(GetCommandLineA(),"--freeze") && gApp.selectedDtc.event!=0x43) return false;
    ID3D11Texture2D *buffer=nullptr,*staging=nullptr;
    if(FAILED(g_pSwapChain->GetBuffer(0,IID_PPV_ARGS(&buffer)))) return false;
    D3D11_TEXTURE2D_DESC desc; buffer->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    if(FAILED(g_pd3dDevice->CreateTexture2D(&desc,nullptr,&staging))) { buffer->Release(); return false; }
    g_pd3dDeviceContext->CopyResource(staging,buffer); buffer->Release();
    D3D11_MAPPED_SUBRESOURCE data;
    if(FAILED(g_pd3dDeviceContext->Map(staging,0,D3D11_MAP_READ,0,&data))) { staging->Release(); return false; }
    const char* name=strstr(GetCommandLineA(),"--freeze")?"freeze-frame.bmp":strstr(GetCommandLineA(),"--empty")?"empty-diagnostics.bmp":"diagnostics.bmp";
    FILE* f=fopen(name,"wb");
    if(f) {
        BITMAPFILEHEADER file={}; BITMAPINFOHEADER info={};
        file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(info); file.bfSize=file.bfOffBits+desc.Width*desc.Height*4;
        info.biSize=sizeof(info); info.biWidth=desc.Width; info.biHeight=-LONG(desc.Height); info.biPlanes=1; info.biBitCount=32;
        fwrite(&file,sizeof(file),1,f); fwrite(&info,sizeof(info),1,f);
        std::vector<unsigned char> line(desc.Width*4);
        for(UINT y=0;y<desc.Height;++y) {
            const auto* row=(const unsigned char*)data.pData+y*data.RowPitch;
            for(UINT x=0;x<desc.Width;++x) { line[x*4]=row[x*4+2]; line[x*4+1]=row[x*4+1]; line[x*4+2]=row[x*4]; line[x*4+3]=255; }
            fwrite(line.data(),1,line.size(),f);
        }
        fclose(f);
    }
    g_pd3dDeviceContext->Unmap(staging,0); staging->Release(); return f!=nullptr;
}
