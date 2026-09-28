static std::string getRecentFilePath() {
    std::string dataDir;
    if (!GetWizardDataDirectory(dataDir)) return "tw_recent.txt";
    return dataDir + "\\tw_recent.txt";
}

void App::loadRecentFiles() {
    recentFiles.clear();
    FILE* f = fopen(getRecentFilePath().c_str(), "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f) && recentFiles.size() < 8) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len > 0) recentFiles.push_back(line);
    }
    fclose(f);
}

void App::saveRecentFiles() {
    std::string dir;
    if (!GetWizardDataDirectory(dir)) return;
    char tempPath[MAX_PATH] = {};
    if (!GetTempFileNameA(dir.c_str(), "lrr", 0, tempPath)) return;
    FILE* f = fopen(tempPath, "w");
    if (!f) { DeleteFileA(tempPath); return; }
    bool ok = true;
    for (const std::string& path : recentFiles) ok = ok && fprintf(f, "%s\n", path.c_str()) > 0;
    const bool flushed = ok && fflush(f) == 0 && _commit(_fileno(f)) == 0;
    const bool closed = fclose(f) == 0;
    const std::string finalPath = getRecentFilePath();
    if (!ok || !flushed || !closed ||
        !MoveFileExA(tempPath, finalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        DeleteFileA(tempPath);
}

void App::addRecentFile(const char* path) {
    std::string p(path);
    for (auto it = recentFiles.begin(); it != recentFiles.end(); ) {
        if (*it == p) it = recentFiles.erase(it);
        else ++it;
    }
    recentFiles.insert(recentFiles.begin(), p);
    if (recentFiles.size() > 8) recentFiles.resize(8);
    saveRecentFiles();
}

int App::allocateTab() {
    for (int i = 0; i < numTabs; ++i) {
        if (!tabs[i].open) {
            tabs[i] = {};
            return i;
        }
    }
    if (numTabs >= MAX_OPEN_TABS) {
        snprintf(statusLine, sizeof(statusLine),
                 "Tab limit reached; close an editor tab and try again");
        return -1;
    }
    return numTabs++;
}

void App::openTable(int tableIndex) {
    // Check if already open
    for (int i = 0; i < numTabs; i++) {
        if (tabs[i].type == OpenTab::TABLE && tabs[i].tableIndex == tableIndex && tabs[i].open) {
            activeTab = i;
            return;
        }
    }
    const int idx = allocateTab();
    if (idx < 0) return;
    auto& t = tabs[idx];
    t.tableIndex = tableIndex;
    t.editorState = {};
    t.editorState.tableIndex = tableIndex;
    t.open = true;
    t.type = OpenTab::TABLE;
    activeTab = idx;
}

void App::openScalars(const char* category) {
    for (int i = 0; i < numTabs; i++) {
        if (tabs[i].type == OpenTab::SCALARS && strcmp(tabs[i].categoryFilter, category) == 0 && tabs[i].open) {
            activeTab = i;
            return;
        }
    }
    const int idx = allocateTab();
    if (idx < 0) return;
    auto& t = tabs[idx];
    t.tableIndex = -1;
    t.open = true;
    t.type = OpenTab::SCALARS;
    strncpy(t.categoryFilter, category, sizeof(t.categoryFilter) - 1);
    activeTab = idx;
}

void App::openFlags(const char* category) {
    for (int i = 0; i < numTabs; i++) {
        if (tabs[i].type == OpenTab::FLAGS && strcmp(tabs[i].categoryFilter, category) == 0 && tabs[i].open) {
            activeTab = i;
            return;
        }
    }
    const int idx = allocateTab();
    if (idx < 0) return;
    auto& t = tabs[idx];
    t.tableIndex = -1;
    t.open = true;
    t.type = OpenTab::FLAGS;
    strncpy(t.categoryFilter, category, sizeof(t.categoryFilter) - 1);
    activeTab = idx;
}

void App::openDashboardTab() {
    for (int i = 0; i < numTabs; i++) {
        if (tabs[i].type == OpenTab::DASHBOARD && tabs[i].open) {
            activeTab = i;
            return;
        }
    }
    const int idx = allocateTab();
    if (idx < 0) return;
    auto& t = tabs[idx];
    t.tableIndex = -1;
    t.open = true;
    t.type = OpenTab::DASHBOARD;
    activeTab = idx;
}

void App::openLoggingTab() {
    for (int i = 0; i < numTabs; i++) {
        if (tabs[i].type == OpenTab::LOGGING && tabs[i].open) {
            activeTab = i;
            return;
        }
    }
    const int idx = allocateTab();
    if (idx < 0) return;
    auto& t = tabs[idx];
    t.tableIndex = -1;
    t.open = true;
    t.type = OpenTab::LOGGING;
    activeTab = idx;
}

void App::openLogViewer() {
    // File open dialog for CSV logs
    char path[260] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = sizeof(path);
    ofn.lpstrDefExt = "csv";
    ofn.Flags       = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return;

    // Switch to tab if this file is already open
    for (int i = 0; i < numTabs; i++) {
        if (tabs[i].type == OpenTab::LOG_VIEWER && tabs[i].open) {
            int idx = tabs[i].logViewerIndex;
            if (idx < (int)logViewers.size() &&
                strcmp(logViewers[idx].log.filePath.c_str(), path) == 0) {
                activeTab = i;
                return;
            }
        }
    }

    const int tabIdx = allocateTab();
    if (tabIdx < 0) return;

    // Parse and add
    logViewers.push_back(LogViewerState{});
    int lvIdx = (int)logViewers.size() - 1;

    if (!ParseLogFile(path, logViewers[lvIdx])) {
        const std::string parseError = logViewers[lvIdx].parseError;
        logViewers.pop_back();
        if (!parseError.empty())
            snprintf(statusLine, sizeof(statusLine), "Could not parse log file: %s", parseError.c_str());
        if (parseError.empty()) snprintf(statusLine, sizeof(statusLine),
                 "Could not parse log file — check it is a valid TW CSV");
        return;
    }

    auto& t = tabs[tabIdx];
    t.tableIndex     = -1;
    t.logViewerIndex = lvIdx;
    t.open           = true;
    t.type           = OpenTab::LOG_VIEWER;
    activeTab        = tabIdx;

    snprintf(statusLine, sizeof(statusLine), "Opened: %s  (%d samples, %.1f s)",
             logViewers[lvIdx].tabLabel.c_str(),
             logViewers[lvIdx].log.sampleCount,
             logViewers[lvIdx].log.duration);
}

static bool FileDialog(char* path, int pathLen, bool save) {
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Binary Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = pathLen;
    ofn.lpstrDefExt = "bin";
    if (save) {
        ofn.Flags = OFN_OVERWRITEPROMPT;
        return GetSaveFileNameA(&ofn) != 0;
    } else {
        ofn.Flags = OFN_FILEMUSTEXIST;
        return GetOpenFileNameA(&ofn) != 0;
    }
}

void App::fileSave() {
    if (cal.filePath[0]) {
        if (cal.saveToFile(cal.filePath)) {
            addRecentFile(cal.filePath);
            snprintf(statusLine, sizeof(statusLine), "Saved: %s", cal.filePath);
        } else {
            snprintf(statusLine, sizeof(statusLine), "Save failed!");
        }
    } else {
        fileSaveAs();
    }
}

void App::fileSaveAs() {
    char path[260] = {};
    if (FileDialog(path, sizeof(path), true)) {
        if (cal.saveToFile(path)) {
            addRecentFile(path);
            snprintf(statusLine, sizeof(statusLine), "Saved: %s", path);
        } else {
            snprintf(statusLine, sizeof(statusLine), "Save failed!");
        }
    }
}

