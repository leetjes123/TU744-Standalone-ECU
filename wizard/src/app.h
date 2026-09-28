#pragma once
#include "serial.h"
#include "protocol.h"
#include "calibration.h"
#include "undo.h"
#include "editor.h"
#include "help.h"
#include "logging.h"
#include "logviewer.h"
#include "firmware_flash.h"
#include "dashboard.h"
#include "storage.h"
#include "theme.h"
#include "tune_transfer.h"
#include "autotune.h"
#include "diagnostics.h"
#define MAX_OPEN_TABS 32
inline float S(float v) { extern float g_dpiScale; return v*g_dpiScale; }
inline int Si(float v) { return int(S(v)); }
struct App {
    HWND hwnd=nullptr;
    SerialPort serial;
    EcuProtocol ecu;
    FirmwareFlasher firmwareFlasher;
    TuneTransfer transfer;
    enum class TransferState { Idle, Active } transferState=TransferState::Idle;
    enum class Operation { Read, Compare, Write, Save } operation=Operation::Read;
    CalBuffer cal;
    AutoTune autotune;
    Diagnostics diagnostics;
    DtcRecord selectedDtc;
    ControllerFault selectedControllerFault;
    bool diagnosticPopup=false, controllerPopup=false;
    char diagnosticSearch[128]={};
    int diagnosticFilter=0;
    float elapsed=0;
    UndoStack undo;
    unsigned char ecuData[CAL_SIZE]={},compareData[CAL_SIZE]={};
    bool ecuSynced=false,compareActive=false,liveTuning=false,updateOnly=false;
    uint16_t baselineGeneration=0;
    std::vector<std::string> comPorts,recentFiles;
    int selectedPort=0;
    LogState logState;
    std::vector<LogViewerState> logViewers;
    HelpBook help;
    UiTheme uiTheme=UiTheme::Workshop;
    DashboardLayout dashboardLayout=DashboardLayout::Standard;
    DWORD monitorLastUpdateTick=0;
    char statusLine[256]="Ready",pendingSettingName[96]={},search[96]={};
    bool showAbout=false,showReviewIssues=false,confirmSave=false,confirmFirmwareFlash=false;
    bool flashCompletionHandled=true,backupBeforeFlash=true,flashBackupPending=false;
    char backupPath[260]={};
    std::vector<unsigned char> pendingFirmwareImage;
    FirmwareImageInfo pendingFirmwareInfo;
    enum class PendingAction { None, Open, Close, Read } pendingAction=PendingAction::None;
    struct OpenTab {
        int tableIndex=-1;
        TableEditorState editorState;
        bool open=false;
        enum SpecialType { TABLE, SCALARS, FLAGS, DASHBOARD, LOGGING, LOG_VIEWER, DIAGNOSTICS, AUTOTUNE } type=TABLE;
        char categoryFilter[64]={};
        int logViewerIndex=0;
    } tabs[MAX_OPEN_TABS];
    int numTabs=0,activeTab=-1;
    void init();
    void update(float dt);
    void drawUI();
    void guardedClose();
    void guardedOpen();
    void performPending();
    void fileOpen();
    void fileSave();
    void fileSaveAs();
    void fileCompare();
    void ecuRead();
    bool ecuWrite();
    void ecuSave();
    bool beginOperation(Operation op);
    void firmwareFlashSelect();
    void beginFlash();
    void connect();
    void disconnect();
    int allocateTab();
    void openTable(int);
    void openScalars(const char*);
    void openFlags(const char*);
    void openDashboardTab();
    void openLoggingTab();
    void openToolTab(OpenTab::SpecialType);
    void beginDiagnostics(bool clear=false);
    void drawDiagnostics();
    void drawAutotune();
    void openLogViewer();
    void loadRecentFiles();
    void saveRecentFiles();
    void addRecentFile(const char*);
    bool ecuBusy() const { return transfer.busy() || firmwareFlasher.busy() || diagnostics.busy(); }
};
extern App gApp;
