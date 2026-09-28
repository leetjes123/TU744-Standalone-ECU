// Tuning Wizard - ECU Calibration Tool
// DX11 + ImGui application

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_freetype.h"
#include <d3d11.h>
#include <windows.h>
#include <tchar.h>

#include "app.h"
#include "theme.h"
#include <shellscalingapi.h>
#pragma comment(lib, "shcore.lib")

// Global DPI scale (1.0 = 96dpi, 1.5 = 144dpi, 2.0 = 192dpi)
float g_dpiScale = 1.0f;

// DX11 globals
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static float                    g_PendingDpiScale = 0.0f;
#ifdef TW_UI_PREVIEW
#include "../tests/diagnostics_preview.inc"
#endif

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 0;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevelArray, 2, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext)  { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)         { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_DPICHANGED: {
        // Resize window to suggested rect when DPI changes (e.g. drag to another monitor)
        RECT* suggested = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        g_PendingDpiScale = HIWORD(wParam) / 96.0f;
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = static_cast<LONG>(900.0f * g_dpiScale);
        info->ptMinTrackSize.y = static_cast<LONG>(600.0f * g_dpiScale);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // Disable ALT application menu
        break;
    case WM_CLOSE:
        gApp.guardedClose();
        return 0; // always handle ourselves (dirty guard may defer)
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void SetupStyle(float dpiScale = 1.0f) {
    ApplyUiTheme(UiTheme::Workshop, dpiScale);
    return;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Restrained workshop theme: neutral surfaces and sparse status color.
    colors[ImGuiCol_Text]              = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]      = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_WindowBg]          = ImVec4(0.105f, 0.11f, 0.115f, 1.00f);
    colors[ImGuiCol_ChildBg]           = ImVec4(0.125f, 0.13f, 0.135f, 1.00f);
    colors[ImGuiCol_PopupBg]           = ImVec4(0.10f, 0.10f, 0.12f, 0.98f);
    colors[ImGuiCol_Border]            = ImVec4(0.35f, 0.35f, 0.40f, 0.80f); // Subtle metallic border
    colors[ImGuiCol_BorderShadow]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]           = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]    = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgActive]     = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBg]           = ImVec4(0.13f, 0.14f, 0.145f, 1.00f);
    colors[ImGuiCol_TitleBgActive]     = ImVec4(0.17f, 0.18f, 0.185f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]  = ImVec4(0.11f, 0.115f, 0.12f, 1.00f);
    colors[ImGuiCol_MenuBarBg]         = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]       = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]     = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_CheckMark]         = ImVec4(0.95f, 0.85f, 0.15f, 1.00f); // Yellow accent
    colors[ImGuiCol_SliderGrab]        = ImVec4(0.95f, 0.85f, 0.15f, 1.00f); // Yellow
    colors[ImGuiCol_SliderGrabActive]  = ImVec4(0.85f, 0.75f, 0.10f, 1.00f);
    colors[ImGuiCol_Button]            = ImVec4(0.20f, 0.21f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered]     = ImVec4(0.27f, 0.28f, 0.29f, 1.00f);
    colors[ImGuiCol_ButtonActive]      = ImVec4(0.95f, 0.85f, 0.15f, 1.00f); // Yellow active
    colors[ImGuiCol_Header]            = ImVec4(0.22f, 0.23f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered]     = ImVec4(0.30f, 0.31f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive]      = ImVec4(0.95f, 0.85f, 0.15f, 0.80f);
    colors[ImGuiCol_Separator]         = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]  = ImVec4(0.15f, 0.28f, 0.55f, 1.00f);
    colors[ImGuiCol_SeparatorActive]   = ImVec4(0.95f, 0.85f, 0.15f, 1.00f);
    colors[ImGuiCol_Tab]               = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TabHovered]        = ImVec4(0.15f, 0.28f, 0.55f, 1.00f);
    colors[ImGuiCol_TabSelected]       = ImVec4(0.10f, 0.20f, 0.40f, 1.00f); // Blue selected tab
    colors[ImGuiCol_TabDimmed]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]     = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight]  = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]    = ImVec4(0.95f, 0.85f, 0.15f, 0.30f);

    style.WindowRounding    = 2.0f;
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(8, 5);
    style.ItemSpacing       = ImVec2(8, 6);
    style.IndentSpacing     = 20.0f;
    style.ScrollbarSize     = 14.0f;
    style.TabBarBorderSize  = 2.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;

    // Scale all sizes for DPI
    style.ScaleAllSizes(dpiScale);
}

static float GetDpiScale(HWND hwnd) {
    // Try per-monitor DPI first (Windows 8.1+)
    HMODULE shcore = GetModuleHandleA("shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *PFN)(HMONITOR, int, UINT*, UINT*);
        PFN getDpiForMonitor = (PFN)GetProcAddress(shcore, "GetDpiForMonitor");
        if (getDpiForMonitor) {
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            UINT dpiX = 96, dpiY = 96;
            if (SUCCEEDED(getDpiForMonitor(mon, 0/*MDT_EFFECTIVE_DPI*/, &dpiX, &dpiY)))
                return dpiX / 96.0f;
        }
    }
    // Fallback: system DPI
    HDC hdc = GetDC(nullptr);
    float dpi = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, hdc);
    return dpi;
}

static void RebuildFonts(float dpiScale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static ImVector<ImWchar> glyphRanges;
    glyphRanges.clear();
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    static const ImWchar extras[] = {0x2000,0x206F,0x20A0,0x20CF,0x2100,0x214F,0x2190,0x21FF,0};
    builder.AddRanges(extras); builder.BuildRanges(&glyphRanges);
    ImFontConfig config;
    config.OversampleH = config.OversampleV = 1; config.PixelSnapH = true;
    config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint;
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f * dpiScale,
                                      &config, glyphRanges.Data)) {
        ImFontConfig fallback; fallback.SizePixels = 14.0f * dpiScale;
        io.Fonts->AddFontDefault(&fallback);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Enable per-monitor DPI awareness via API (more reliable than manifest alone)
    ImGui_ImplWin32_EnableDpiAwareness();

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TuningWizard";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // Get initial DPI scale for window sizing
    float dpiScale = 1.0f;
    {
        HDC hdc = GetDC(nullptr);
        dpiScale = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
        ReleaseDC(nullptr, hdc);
    }

    int winW = (int)(1400 * dpiScale);
    int winH = (int)(900 * dpiScale);
    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int workW = workArea.right - workArea.left;
    const int workH = workArea.bottom - workArea.top;
    if (winW > workW - 24) winW = workW - 24;
    if (winH > workH - 24) winH = workH - 24;
    const int winX = workArea.left + (workW - winW) / 2;
    const int winY = workArea.top + (workH - winH) / 2;

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"Tuning Wizard - Standalone ECU Calibration",
        WS_OVERLAPPEDWINDOW,
        winX, winY, winW, winH,
        nullptr, nullptr, hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    // A tuning workstation needs predictable room for 16x16 maps and live channels.
    // The user can restore and resize normally after startup.
    ShowWindow(hwnd, nCmdShow == SW_HIDE ? SW_HIDE : (nCmdShow == SW_SHOWMINIMIZED ? SW_RESTORE : SW_MAXIMIZE));
    UpdateWindow(hwnd);

    // Refine DPI scale now that we have a window
    dpiScale = GetDpiScale(hwnd);
    g_dpiScale = dpiScale;

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Segoe UI is the native Windows desktop face and suits dense technical tooling.
    // With FreeType, hinting replaces oversampling — 1x1 gives sharper results than 4x4.
    {
        float fontSize = 16.0f * dpiScale;
        const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
        static ImVector<ImWchar> glyphRanges;
        ImFontGlyphRangesBuilder glyphBuilder;
        glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        static const ImWchar extraRanges[] = {
            0x2000, 0x206F, // typographic punctuation
            0x20A0, 0x20CF, // currency and technical symbols
            0x2100, 0x214F, // letterlike symbols, including ohm
            0x2190, 0x21FF, // arrows
            0
        };
        glyphBuilder.AddRanges(extraRanges);
        glyphBuilder.BuildRanges(&glyphRanges);
        ImFontConfig fontCfg;
        fontCfg.OversampleH       = 1;
        fontCfg.OversampleV       = 1;
        fontCfg.PixelSnapH        = true;
        fontCfg.FontBuilderFlags  = ImGuiFreeTypeBuilderFlags_ForceAutoHint;
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath, fontSize, &fontCfg, glyphRanges.Data);
        if (!font) {
            ImFontConfig cfg;
            cfg.SizePixels = 14.0f * dpiScale;
            io.Fonts->AddFontDefault(&cfg);
        }
    }

    SetupStyle(dpiScale);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // App init
    gApp.hwnd = hwnd;
    gApp.init();
#ifdef TW_UI_PREVIEW
    InitDiagnosticPreview();
    if(strstr(GetCommandLineA(),"--narrow")) SetWindowPos(hwnd,nullptr,0,0,850,780,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    int previewFrames=0;
#endif

    // Main loop
    LARGE_INTEGER freq, lastTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    ImVec4 clearColor(0.08f, 0.08f, 0.10f, 1.00f);
    bool done = false;

    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Handle resize
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        if (g_PendingDpiScale > 0.0f) {
            g_dpiScale = g_PendingDpiScale;
            g_PendingDpiScale = 0.0f;
            ApplyUiTheme(gApp.uiTheme, g_dpiScale);
            ImGui_ImplDX11_InvalidateDeviceObjects();
            RebuildFonts(g_dpiScale);
            ImGui_ImplDX11_CreateDeviceObjects();
        }

        // Calculate delta time
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        float dt = (float)(currentTime.QuadPart - lastTime.QuadPart) / (float)freq.QuadPart;
        lastTime = currentTime;

        // Update app
        gApp.update(dt);

        // Render
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
#ifdef TW_UI_PREVIEW
        DiagnosticPreviewInput(previewFrames);
#endif
        ImGui::NewFrame();

        gApp.drawUI();

        ImGui::Render();
        const float clearColorArr[4] = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColorArr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#ifdef TW_UI_PREVIEW
        if(++previewFrames==8) {
            if(!SaveDiagnosticPreview()) return 3;
            done=true;
        }
#endif

        g_pSwapChain->Present(1, 0); // VSync

        // Reduce frame rate when disconnected and idle (saves CPU/GPU)
        if (!gApp.serial.isOpen() && gApp.transferState == App::TransferState::Idle) {
            Sleep(8); // ~40fps when disconnected
        }
    }

    // Cleanup
    gApp.logState.stopRecording();
    gApp.ecu.destroyThread();
    gApp.serial.close();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);

    return 0;
}
