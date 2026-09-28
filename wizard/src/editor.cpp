#include "editor.h"
#include "app.h"
#include "grid_paste.h"
#include "tooltips.h"
#include "protocol.h"
#include "theme.h"
#include "ui_helpers.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cfloat>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <windows.h>

// Clipboard storage (tab-separated text)
static std::string clipboardData;

static bool ParseStrictFloat(const char* text, float& value) {
    if (!text) return false;
    while (std::isspace((unsigned char)*text)) ++text;
    if (!*text) return false;
    errno = 0;
    char* end = nullptr;
    value = strtof(text, &end);
    if (end == text || errno == ERANGE || !std::isfinite(value)) return false;
    while (std::isspace((unsigned char)*end)) ++end;
    return *end == '\0';
}

static bool AxisValueIsOrdered(const AxisDef& axis, const CalBuffer& cal,
                               int index, float value) {
    return cal.axisValueIsOrdered(axis, index, value);
}

ImU32 CellColor(float val, float minVal, float maxVal) {
    float t = (maxVal > minVal) ? (val - minVal) / (maxVal - minVal) : 0.5f;
    t = std::clamp(t, 0.0f, 1.0f);

    // Smooth HSV sweep: blue (H=0.66) → cyan → green → yellow → red (H=0.0)
    const bool highContrast = CurrentUiTheme() == UiTheme::HighContrast;
    const ImVec4 low = highContrast ? ImVec4(0.08f,0.08f,0.08f,1) : ImVec4(0.18f,0.12f,0.38f,1);
    const ImVec4 mid = highContrast ? ImVec4(0.45f,0.45f,0.45f,1) : ImVec4(0.10f,0.58f,0.55f,1);
    const ImVec4 high = highContrast ? ImVec4(0.94f,0.94f,0.94f,1) : ImVec4(0.95f,0.82f,0.28f,1);
    const float u = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
    const ImVec4& a = t < 0.5f ? low : mid;
    const ImVec4& bcol = t < 0.5f ? mid : high;
    float r = a.x + (bcol.x - a.x) * u;
    float g = a.y + (bcol.y - a.y) * u;
    float b = a.z + (bcol.z - a.z) * u;
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 180);
}

static float GetAxisValue(const AxisDef* axis, const CalBuffer& cal, int index) {
    if (!axis) return (float)index;
    return cal.readAxisValue(*axis, index);
}

static bool IsAxisEditable(const AxisDef* axis) {
    return axis && axis->offset >= 0 || (axis && axis->offset == -2);
}

// Find the axis index closest to a given value (for live cell highlighting)
static int FindAxisIndex(const AxisDef* axis, const CalBuffer& cal, float value) {
    if (!axis || axis->count <= 0) return -1;

    // Find closest index
    int best = 0;
    float bestDist = 1e9f;
    for (int i = 0; i < axis->count; i++) {
        float axVal = GetAxisValue(axis, cal, i);
        float dist = fabsf(value - axVal);
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

// Map monitor data to the value a particular axis represents
static float GetMonitorValueForAxis(const AxisDef* axis, const MonitorData* mon) {
    if (!axis || !mon || !mon->valid) return -1e9f;
    if (axis == &AXIS_RPM || axis == &AXIS_ACCEL_AMOUNT_RPM)
        return (float)mon->rpm;
    if (axis == &AXIS_KPA)
        return (float)mon->kpa;
    if (axis == &AXIS_TPS || axis == &AXIS_ACCEL_RATE_Y)
        return (float)mon->tps;
    if (axis == &AXIS_TEMP)
        return (float)mon->clt;
    if (axis == &AXIS_VOLTAGE) {
        return mon->battery;
    }
    return -1e9f;
}

// ============================================================
//  Toolbar
// ============================================================

static void SetHoverTooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", text);
}

static void ApplyPasteOutcome(TableEditorState& state, const PasteOutcome& outcome) {
    strncpy(state.pasteStatus, outcome.message.c_str(), sizeof(state.pasteStatus) - 1);
    state.pasteStatus[sizeof(state.pasteStatus) - 1] = '\0';
    state.pasteStatusIsError = !outcome.ok;
}

static bool DrawToolbar(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                        TableEditorState& state) {
    bool modified = false;
    CellSelection& sel = state.sel;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6), S(4)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(3), S(4)));

    const bool subdued = !sel.active;
    if (subdued) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);

    // Value input field. The hint names it: without one it reads as a stray box.
    ImGui::SetNextItemWidth(S(80));
    bool enterPressed = ImGui::InputTextWithHint("##tbval", "value", state.toolbarValue,
                                                 sizeof(state.toolbarValue),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
    SetHoverTooltip("Value used by Set / Add / Sub / Mul / Div / Scale on the selected cells.");
    float val = 0.0f;
    const bool validValue = ParseStrictFloat(state.toolbarValue, val);
    DrawInvalidInputOutline(state.toolbarValue[0] && !validValue);
    if (state.toolbarValue[0] && !validValue)
        strncpy(state.inputError, "Enter one finite numeric value", sizeof(state.inputError) - 1);
    else if (validValue)
        state.inputError[0] = '\0';

    ImGui::SameLine();

    // Set
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.25f, 1.0f));
    if ((ImGui::Button("= Set") || enterPressed) && sel.active && validValue) {
        SetSelection(table, cal, undo, sel, val);
        modified = true;
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // Add
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.35f, 0.55f, 1.0f));
    if (ImGui::Button("+ Add") && sel.active && validValue) {
        AddToSelection(table, cal, undo, sel, val);
        modified = true;
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // Subtract
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.35f, 0.55f, 1.0f));
    if (ImGui::Button("- Sub") && sel.active && validValue) {
        AddToSelection(table, cal, undo, sel, -val);
        modified = true;
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // Multiply
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.35f, 0.20f, 1.0f));
    if (ImGui::Button("* Mul") && sel.active && validValue && val != 0.0f) {
        MultiplySelection(table, cal, undo, sel, val);
        modified = true;
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // Divide
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.35f, 0.20f, 1.0f));
    if (ImGui::Button("/ Div") && sel.active && validValue && val != 0.0f) {
        DivideSelection(table, cal, undo, sel, val);
        modified = true;
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // Scale % (e.g. 110 = multiply by 1.10, 90 = multiply by 0.90)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.30f, 0.50f, 1.0f));
    if (ImGui::Button("% Scale") && sel.active && validValue) {
        float factor = val / 100.0f;
        if (factor > 0.0f) {
            MultiplySelection(table, cal, undo, sel, factor);
            modified = true;
        }
    }
    ImGui::PopStyleColor();

    if (state.inputError[0]) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", state.inputError);
    }

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.4f, 1.0f), "|");
    ImGui::SameLine();

    // Shape operations act on the selection, so they dim with it.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.45f, 1.0f));
    if (ImGui::Button("Interpolate...") && sel.isMulti()) ImGui::OpenPopup("InterpolationMenu");
    if (ImGui::BeginPopup("InterpolationMenu")) {
        if (ImGui::MenuItem("Across rows", nullptr, false, sel.countCols() > 1)) {
            InterpolateRows(table, cal, undo, sel); modified = true;
        }
        if (ImGui::MenuItem("Across columns", nullptr, false, sel.countRows() > 1)) {
            InterpolateColumns(table, cal, undo, sel); modified = true;
        }
        if (ImGui::MenuItem("Bilinear (corner values)", nullptr, false,
                            sel.countRows() > 1 && sel.countCols() > 1)) {
            InterpolateSelection(table, cal, undo, sel); modified = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    if (!sel.isMulti())
        SetHoverTooltip("Select two or more cells to interpolate between them.");

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.45f, 1.0f));
    if (ImGui::Button("Smooth") && sel.isMulti()) {
        SmoothSelection(table, cal, undo, sel);
        modified = true;
    }
    ImGui::PopStyleColor();
    SetHoverTooltip(sel.isMulti()
        ? "Average each selected cell with its neighbours to take the edges off a map."
        : "Select two or more cells to smooth them.");

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.4f, 1.0f), "|");
    ImGui::SameLine();

    // Copy needs a selection; the rest below do not.
    if (ImGui::Button("Copy") && sel.active) {
        CopySelection(table, cal, sel);
    }
    if (!sel.active) SetHoverTooltip("Select cells to copy them.");

    if (subdued) ImGui::PopStyleVar();

    // Paste never needs a selection: a full-size grid fills the whole table.
    ImGui::SameLine();
    if (ImGui::Button("Paste")) {
        const PasteOutcome outcome = PasteSelection(table, cal, undo, sel);
        ApplyPasteOutcome(state, outcome);
        modified |= outcome.modified;
    }
    SetHoverTooltip("Paste values from the clipboard (Ctrl+V). A full-size grid "
                    "replaces the whole table; a single value fills the selection.");

    ImGui::SameLine();
    if (ImGui::Button("Paste Grid...")) state.openPasteGrid = true;
    SetHoverTooltip("Open a box to paste a whole map into, with a preview, a "
                    "replace/add/multiply choice and optional axis update (Ctrl+Shift+V).");

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.4f, 1.0f), "|");
    ImGui::SameLine();

    // Select All always works, so it is never dimmed.
    if (ImGui::Button("Sel All")) {
        const int visualRows = table.transposed ? table.cols : table.rows;
        const int visualCols = table.transposed ? table.rows : table.cols;
        sel.startRow = 0;
        sel.startCol = 0;
        sel.endRow = visualRows - 1;
        sel.endCol = visualCols - 1;
        sel.active = true;
    }
    SetHoverTooltip("Select every cell (Ctrl+A).");


    // Selection info
    ImGui::SameLine();
    if (sel.active) {
        int nR = sel.countRows(), nC = sel.countCols();
        float minValue = cal.readTableCell(table, sel.minRow(), sel.minCol());
        float maxValue = minValue;
        double total = 0.0;
        for (int r = sel.minRow(); r <= sel.maxRow(); ++r)
            for (int c = sel.minCol(); c <= sel.maxCol(); ++c) {
                const float current = cal.readTableCell(table, r, c);
                minValue = std::min(minValue, current);
                maxValue = std::max(maxValue, current);
                total += current;
            }
        const float average = (float)(total / sel.countCells());
        if (nR == 1 && nC == 1) {
            float v = cal.readTableCell(table, sel.endRow, sel.endCol);
            ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.8f, 1.0f), "Cell: %g %s", v, table.units);
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.8f, 1.0f),
                               "%dx%d / %d cells  min %g  max %g  avg %g %s",
                               nC, nR, sel.countCells(), minValue, maxValue, average, table.units);
        }
        if (validValue && sel.isMulti()) {
            auto clampValue = [&](float v) { return std::max(table.minVal, std::min(table.maxVal, v)); };
            const float addMin = clampValue(minValue + val), addMax = clampValue(maxValue + val);
            const float subMin = clampValue(minValue - val), subMax = clampValue(maxValue - val);
            const float mulA = clampValue(minValue * val), mulB = clampValue(maxValue * val);
            const float mulMin = std::min(mulA, mulB), mulMax = std::max(mulA, mulB);
            ImGui::TextDisabled("Preview (%d cells): Set %g | Add %g-%g | Sub %g-%g | Mul %g-%g %s",
                                sel.countCells(), clampValue(val), addMin, addMax, subMin, subMax,
                                mulMin, mulMax, table.units);
            if (addMin == table.minVal || addMax == table.maxVal || subMin == table.minVal ||
                subMax == table.maxVal || mulMin == table.minVal || mulMax == table.maxVal)
                ImGui::TextColored(ImVec4(0.93f,0.67f,0.20f,1), "WARNING: one or more preview outcomes reach a table limit.");
        }
    }

    // Paste result, so a paste never fails silently.
    if (state.pasteStatus[0]) {
        ImGui::TextColored(state.pasteStatusIsError ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                                    : ImVec4(0.45f, 0.85f, 0.55f, 1.0f),
                           "%s", state.pasteStatus);
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss##paste")) state.pasteStatus[0] = '\0';
    }

    ImGui::PopStyleVar(2);
    return modified;
}

// ============================================================
//  Paste Grid dialog
// ============================================================

static std::string ReadClipboardText();
bool AxisValuesUsable(const AxisDef* axis, const std::vector<float>& values, int expected);

// A scratch box for a whole map: paste text in, see exactly how it will be read,
// then apply. Returns true if the calibration was modified.
static bool DrawPasteGridDialog(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                                TableEditorState& state) {
    const int visualRows = table.transposed ? table.cols : table.rows;
    const int visualCols = table.transposed ? table.rows : table.cols;

    if (state.openPasteGrid) {
        state.openPasteGrid = false;
        // Start from the clipboard so the common case is one click away.
        const std::string clip = ReadClipboardText();
        state.pasteGridText.assign(clip.begin(), clip.end());
        state.pasteGridText.resize(clip.size() + 8192, '\0');
        ImGui::OpenPopup("Paste Grid");
    }
    if (state.pasteGridText.empty()) state.pasteGridText.resize(8192, '\0');

    bool modified = false;
    ImGui::SetNextWindowSize(ImVec2(S(680), 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Paste Grid", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return false;

    ImGui::Text("%s - expects %d columns x %d rows.", table.name, visualCols, visualRows);
    ImGui::TextDisabled("Values may be separated by tabs, commas, semicolons or spaces. An axis "
                        "header row or column is recognised. Results outside %g-%g %s are clamped.",
                        table.minVal, table.maxVal, table.units);

    if (ImGui::Button("Reload from clipboard")) {
        const std::string clip = ReadClipboardText();
        state.pasteGridText.assign(clip.begin(), clip.end());
        state.pasteGridText.resize(clip.size() + 8192, '\0');
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) std::fill(state.pasteGridText.begin(), state.pasteGridText.end(), '\0');

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.4f, 1.0f), "|");
    ImGui::SameLine();
    ImGui::TextUnformatted("Apply as:");
    struct ModeChoice { const char* label; PasteMode mode; const char* tip; };
    static const ModeChoice modes[] = {
        {"Replace",  PasteMode::Replace,  "Pasted values become the new cell values."},
        {"Add to",   PasteMode::Add,      "Each pasted value is added to the cell (use negatives to subtract)."},
        {"Multiply", PasteMode::Multiply, "Each cell is multiplied by the pasted value (1.05 = +5%)."},
    };
    for (const ModeChoice& choice : modes) {
        ImGui::SameLine();
        if (ImGui::RadioButton(choice.label, state.pasteMode == choice.mode))
            state.pasteMode = choice.mode;
        SetHoverTooltip(choice.tip);
    }

    ImGui::InputTextMultiline("##pastegrid", state.pasteGridText.data(), state.pasteGridText.size(),
                              ImVec2(-FLT_MIN, S(240)));

    GridPasteRequest req;
    req.targetRows = visualRows;
    req.targetCols = visualCols;
    req.minVal = (state.pasteMode == PasteMode::Replace) ? table.minVal : 0.0f;
    req.maxVal = (state.pasteMode == PasteMode::Replace) ? table.maxVal : 0.0f;
    const std::string text(state.pasteGridText.data());
    const GridPasteParse parsed = ParseGridPaste(text, req);

    const bool boxIsEmpty = text.find_first_not_of(" \t\r\n") == std::string::npos;
    if (boxIsEmpty) {
        // An empty box is not a mistake yet - say what to do, don't scold.
        ImGui::TextDisabled("Paste your values here (Ctrl+V), or copy them and press "
                            "\"Reload from clipboard\".");
    } else if (!parsed.ok) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", parsed.error.c_str());
    } else {
        const bool fullTable = (parsed.rows == visualRows && parsed.cols == visualCols);
        if (fullTable)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f),
                               "Read %d x %d - fills the whole table.", parsed.cols, parsed.rows);
        else
            ImGui::TextColored(ImVec4(0.93f, 0.67f, 0.20f, 1.0f),
                               "Read %d x %d - table is %d x %d, so this lands at row %d, column %d "
                               "and anything past the edge is dropped.",
                               parsed.cols, parsed.rows, visualCols, visualRows,
                               (state.sel.active ? state.sel.minRow() : 0) + 1,
                               (state.sel.active ? state.sel.minCol() : 0) + 1);
        if (parsed.strippedHeaderRow || parsed.strippedHeaderCol)
            ImGui::TextDisabled("Read the leading %s%s%s as axis labels.",
                                parsed.strippedHeaderRow ? "row" : "",
                                (parsed.strippedHeaderRow && parsed.strippedHeaderCol) ? " and " : "",
                                parsed.strippedHeaderCol ? "column" : "");

        // Offer the axes only when the labels really could be breakpoints.
        const AxisDef* xAxis = table.xAxis ? &ResolveAxis(*table.xAxis, cal.data) : nullptr;
        const AxisDef* yAxis = table.yAxis ? &ResolveAxis(*table.yAxis, cal.data) : nullptr;
        const bool xUsable = AxisValuesUsable(xAxis, parsed.headerRow, parsed.cols);
        const bool yUsable = AxisValuesUsable(yAxis, parsed.headerCol, parsed.rows);
        if (xUsable || yUsable) {
            char axisLabel[128];
            snprintf(axisLabel, sizeof(axisLabel), "Also update the %s%s%s breakpoints",
                     xUsable ? xAxis->name : "", (xUsable && yUsable) ? " and " : "",
                     yUsable ? yAxis->name : "");
            ImGui::Checkbox(axisLabel, &state.pasteApplyAxes);
            SetHoverTooltip("Writes the header values into the table's own axis, so a map and its "
                            "breakpoints move together.");
        } else if (parsed.strippedHeaderRow || parsed.strippedHeaderCol) {
            state.pasteApplyAxes = false;
            ImGui::TextDisabled("The labels cannot be used as breakpoints (wrong count, not "
                                "increasing, or this axis is fixed), so only the values are pasted.");
        }
        if (parsed.clamped)
            ImGui::TextColored(ImVec4(0.93f, 0.67f, 0.20f, 1.0f),
                               "%d value(s) are outside %g-%g %s and will be clamped.",
                               parsed.clamped, table.minVal, table.maxVal, table.units);

        // First rows of the parsed grid, so a column shift is obvious before applying.
        if (ImGui::BeginTable("##pastepreview", parsed.cols + 1,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_ScrollX, ImVec2(-FLT_MIN, S(150)))) {
            ImGui::TableSetupColumn("");
            for (int c = 0; c < parsed.cols; ++c) {
                char label[16];
                snprintf(label, sizeof(label), "%d", c + 1);
                ImGui::TableSetupColumn(label);
            }
            ImGui::TableHeadersRow();
            for (int r = 0; r < parsed.rows; ++r) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", r + 1);
                for (int c = 0; c < parsed.cols; ++c) {
                    ImGui::TableNextColumn();
                    const bool fits = (r < visualRows && c < visualCols);
                    if (fits) ImGui::Text("%g", parsed.at(r, c));
                    else ImGui::TextDisabled("%g", parsed.at(r, c));
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!parsed.ok);
    if (ImGui::Button("Apply", ImVec2(S(120), 0))) {
        const PasteOutcome outcome = PasteGridText(table, cal, undo, state.sel, text,
                                                   state.pasteMode, state.pasteApplyAxes);
        ApplyPasteOutcome(state, outcome);
        modified = outcome.modified;
        if (outcome.ok) ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(S(120), 0))) ImGui::CloseCurrentPopup();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    return modified;
}

// ============================================================
//  Table Editor
// ============================================================

// Helper: read raw cell value from a data buffer at a given offset
static int readRawCell(const unsigned char* data, int offset, CellType type) {
    switch (type) {
        case CellType::U8:    return data[offset];
        case CellType::S8:    return (signed char)data[offset];
        case CellType::U16BE: return (data[offset] << 8) | data[offset + 1];
        case CellType::S16BE: { int v = (data[offset] << 8) | data[offset + 1]; return (v > 32767) ? v - 65536 : v; }
        default: return data[offset];
    }
}

bool DrawTableEditor(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                     TableEditorState& state, const MonitorData* mon,
                     const unsigned char* compareData) {
    ImGui::PushID(table.offset); // unique ID scope per table

    const AxisDef* visualXAxis = table.xAxis ? &ResolveAxis(*table.xAxis, cal.data) : nullptr;
    const AxisDef* visualYAxis = table.yAxis ? &ResolveAxis(*table.yAxis, cal.data) : nullptr;
    bool modified = false;
    bool is1D = (table.rows == 1);
    // Visual dimensions: when transposed, swap data rows/cols for display
    int rows = table.transposed ? table.cols : table.rows;
    int cols = table.transposed ? table.rows : table.cols;

    // Keyboard shortcuts
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        // Copy/paste must not auto-repeat: holding the keys down a moment too
        // long would otherwise stack several identical pastes on the undo stack,
        // so the first Ctrl+Z appears to do nothing.
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            CopySelection(table, cal, state.sel);
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            if (ImGui::GetIO().KeyShift) {
                state.openPasteGrid = true;
            } else {
                const PasteOutcome outcome = PasteSelection(table, cal, undo, state.sel);
                ApplyPasteOutcome(state, outcome);
                modified |= outcome.modified;
            }
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            undo.undo(cal.data);
            cal.recomputeDirty();
            modified = true;
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            undo.redo(cal.data);
            cal.recomputeDirty();
            modified = true;
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
            state.sel.startRow = 0; state.sel.startCol = 0;
            state.sel.endRow = rows - 1; state.sel.endCol = cols - 1;
            state.sel.active = true;
        }

        // Arrow key navigation
        if (state.sel.active && !state.editing && !state.editingAxis) {
            // Ctrl+arrow nudges values instead of moving the cursor.
            int dr = 0, dc = 0;
            if (!ImGui::GetIO().KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    dr = -1;
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  dr = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  dc = -1;
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dc = 1;
                if (dr || dc) {
                    int nr = std::clamp(state.sel.endRow + dr, 0, rows - 1);
                    int nc = std::clamp(state.sel.endCol + dc, 0, cols - 1);
                    if (ImGui::GetIO().KeyShift) {
                        state.sel.extendTo(nr, nc);
                    } else {
                        state.sel.select(nr, nc);
                    }
                }
            }

            // Enter/F2 to edit single cell
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_F2)) {
                if (!state.sel.isMulti()) {
                    state.editing = true;
                    state.editFocusNeeded = true;
                    state.editSelectAll = true;
                    state.editRow = state.sel.endRow;
                    state.editCol = state.sel.endCol;
                    float v = cal.readTableCell(table, state.editRow, state.editCol);
                    snprintf(state.editBuf, sizeof(state.editBuf), "%g", v);
                }
            }

            // Nudge the selection by whole raw units. Keypad +/- and Ctrl+Up/Down
            // are used because the plain -/digit keys already start typing a value.
            {
                int nudge = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) nudge = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) nudge = -1;
                if (ImGui::GetIO().KeyCtrl) {
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge = 1;
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge = -1;
                }
                if (nudge) {
                    if (ImGui::GetIO().KeyShift) nudge *= 10;
                    NudgeSelection(table, cal, undo, state.sel, nudge);
                    modified = true;
                }
            }

            // Delete to zero
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                int sz = cal.cellSize(table.cellType);
                undo.record(cal.data, table.offset, rows * cols * sz, "Delete");
                for (int r = state.sel.minRow(); r <= state.sel.maxRow(); r++)
                    for (int c = state.sel.minCol(); c <= state.sel.maxCol(); c++)
                        cal.writeTableCell(table, r, c, 0);
                undo.finalize(cal.data, table.offset, rows * cols * sz);
                modified = true;
            }
        }

        // Type a number to start editing (single cell) or fill toolbar (multi)
        if (state.sel.active && !state.editing && !state.editingAxis) {
            char typedChar = 0;
            for (int k = ImGuiKey_0; k <= ImGuiKey_9; k++) {
                if (ImGui::IsKeyPressed((ImGuiKey)k)) {
                    typedChar = '0' + (k - ImGuiKey_0);
                    break;
                }
            }
            if (!typedChar && ImGui::IsKeyPressed(ImGuiKey_Minus))  typedChar = '-';
            if (!typedChar && ImGui::IsKeyPressed(ImGuiKey_Period)) typedChar = '.';

            if (typedChar) {
                if (state.sel.isMulti()) {
                    // Append to toolbar value (accumulate keystrokes)
                    int len = (int)strlen(state.toolbarValue);
                    if (len < (int)sizeof(state.toolbarValue) - 1) {
                        state.toolbarValue[len] = typedChar;
                        state.toolbarValue[len + 1] = '\0';
                    }
                } else {
                    state.editing = true;
                    state.editFocusNeeded = true;
                    state.editSelectAll = false; // typed char replaces value
                    state.editRow = state.sel.endRow;
                    state.editCol = state.sel.endCol;
                    state.editBuf[0] = typedChar;
                    state.editBuf[1] = '\0';
                }
            }

            // Backspace: remove last char from toolbar value
            if (state.sel.isMulti() && state.toolbarValue[0] &&
                ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                int len = (int)strlen(state.toolbarValue);
                if (len > 0) state.toolbarValue[len - 1] = '\0';
            }

            // Enter with multi-selection: apply Set with toolbar value
            if (state.sel.isMulti() && state.toolbarValue[0] &&
                ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                float val = 0.0f;
                if (ParseStrictFloat(state.toolbarValue, val)) {
                    SetSelection(table, cal, undo, state.sel, val);
                    state.toolbarValue[0] = '\0';
                    state.inputError[0] = '\0';
                    modified = true;
                } else {
                    strncpy(state.inputError, "Enter one finite numeric value",
                            sizeof(state.inputError) - 1);
                }
            }

            // Escape: clear toolbar value
            if (state.sel.isMulti() && state.toolbarValue[0] &&
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                state.toolbarValue[0] = '\0';
            }
        }
    }

    // Stable context bar: identity, axes, dimensions, compare, and live state.
    ImGui::Text("%s  |  %s", table.name, table.units);
    { const char* tip = GetTooltip(table.name);
      if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip); }
    if (is1D) {
        ImGui::SameLine(), ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "  1x%d", cols);
        if (visualXAxis)
            ImGui::SameLine(), ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.4f, 1.0f), "  X: %s", visualXAxis->name);
    } else {
        ImGui::SameLine(), ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "  %dx%d", cols, rows);
        if (visualXAxis)
            ImGui::SameLine(), ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.4f, 1.0f), "  X: %s", visualXAxis->name);
        if (visualYAxis)
            ImGui::SameLine(), ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.4f, 1.0f), "  Y: %s", visualYAxis->name);
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.48f, 0.68f, 0.72f, 1.0f), "  Compare: %s  |  Live: %s",
                       compareData ? "reference" : "off",
                       mon && mon->valid ? "available" : "unavailable");

    // Toolbar
    if (DrawToolbar(table, cal, undo, state))
        modified = true;

    ImGui::Separator();

    // --- Live cell tracking ---
    int liveCol = -1, liveRow = -1;
    if (mon && mon->valid) {
        float xVal = GetMonitorValueForAxis(visualXAxis, mon);
        if (xVal > -1e8f)
            liveCol = FindAxisIndex(visualXAxis, cal, xVal);
        if (visualYAxis) {
            float yVal = GetMonitorValueForAxis(visualYAxis, mon);
            if (yVal > -1e8f)
                liveRow = FindAxisIndex(visualYAxis, cal, yVal);
        } else {
            liveRow = 0; // 1D tables
        }
    }

    if (mon && mon->valid && mon->synced() && table.offset < 0x300 && table.rows==16) {
        liveCol=mon->cellRpm; liveRow=mon->cellLoad;
        ImGui::Text("Fuel-plan cell: RPM %d + %.3f, load %d + %.3f", mon->cellRpm,
                    mon->rpmFraction, mon->cellLoad, mon->loadFraction);
    }
    // --- Build the table ---
    int totalCols = cols + 1; // +1 for row header
    ImGuiTableFlags tblFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
                               ImGuiTableFlags_NoHostExtendX;

    const float minCellW = (table.cellType == CellType::U16BE || table.cellType == CellType::S16BE)
                         ? S(62) : S(48);

    // Legend, live-cell readout and compare summary sit under the grid.
    const float footerReserve = ImGui::GetTextLineHeightWithSpacing() * 4.0f + S(10);
    const ImVec2 availableGridSpace = ImGui::GetContentRegionAvail();

    // The corner names both axes. It shares the data cells' width, so keep the
    // text compact and hang the full names off a tooltip when it will not fit.
    char cornerLabel[64] = "";
    if (visualYAxis && visualXAxis)
        snprintf(cornerLabel, sizeof(cornerLabel), "%s\\%s", visualYAxis->name, visualXAxis->name);
    else if (visualXAxis)
        snprintf(cornerLabel, sizeof(cornerLabel), "%s", visualXAxis->name);

    // Fill three quarters of the panel: big enough to work in, small enough to
    // leave the surrounding context visible. The axis header row and column are
    // sized exactly like the data cells, so the grid reads as one even mesh.
    const float widthFraction = 0.75f, heightFraction = 0.75f;
    const float cellPadX = ImGui::GetStyle().CellPadding.x * 2.0f;
    // Reserve covers cell padding, one border line per column and the host
    // window's scrollbar: overshooting by a pixel scrolls the whole panel.
    const float widthReserve = cellPadX * totalCols + (float)(totalCols + 1) +
                               ImGui::GetStyle().ScrollbarSize + S(6);
    const float widthForCells = availableGridSpace.x * widthFraction - widthReserve;
    float cellW = minCellW;
    if (totalCols > 0 && widthForCells > 0.0f)
        cellW = std::clamp(widthForCells / totalCols, minCellW, S(120));
    const float headerW = cellW;

    const float textRowHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    const float maxGridHeight = std::max(S(80), availableGridSpace.y - footerReserve);
    float rowHeight = textRowHeight;
    {
        // rows + 1 because the axis header row is now the same height as the rest.
        const float heightForRows = maxGridHeight * heightFraction - (float)(rows + 2) - S(6);
        if (heightForRows > 0.0f)
            rowHeight = std::clamp(heightForRows / (rows + 1), textRowHeight, textRowHeight * 2.0f);
    }
    const float populatedRowHeight = rowHeight;
    const float populatedHeight = rowHeight * (rows + 1) + (float)(rows + 2) + S(3);
    const float populatedWidth = cellW * totalCols + cellPadX * totalCols +
                                 (float)(totalCols + 1) + S(3);
    const float scrollbar = ImGui::GetStyle().ScrollbarSize;
    const float availableWidth = std::max(S(80), availableGridSpace.x - S(3));
    bool needVertical = populatedHeight > maxGridHeight;
    bool needHorizontal = populatedWidth > availableWidth - (needVertical ? scrollbar : 0.0f);
    if (needHorizontal && !needVertical)
        needVertical = populatedHeight > maxGridHeight - scrollbar;
    needHorizontal = populatedWidth > availableWidth - (needVertical ? scrollbar : 0.0f);
    if (needHorizontal) tblFlags |= ImGuiTableFlags_ScrollX;
    if (needVertical) tblFlags |= ImGuiTableFlags_ScrollY;
    const float gridWidth = std::min(availableWidth, populatedWidth + (needVertical ? scrollbar : 0.0f));
    const float gridHeight = std::min(maxGridHeight, populatedHeight + (needHorizontal ? scrollbar : 0.0f));
    float actualMin = cal.readTableCell(table, 0, 0);
    float actualMax = actualMin;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const float current = cal.readTableCell(table, r, c);
            actualMin = std::min(actualMin, current);
            actualMax = std::max(actualMax, current);
        }
    // A flat table has no range to spread colour over; fall back to its limits.
    const float heatMin = (actualMax > actualMin) ? actualMin : table.minVal;
    const float heatMax = (actualMax > actualMin) ? actualMax : table.maxVal;
    if (ImGui::BeginTable("##tbl", totalCols, tblFlags, ImVec2(gridWidth, gridHeight))) {
        ImGui::TableSetupScrollFreeze(needHorizontal ? 1 : 0, needVertical ? 1 : 0);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, headerW); // row header col

        for (int c = 0; c < cols; c++) {
            char hdr[48];
            float axVal = GetAxisValue(visualXAxis, cal, c);
            snprintf(hdr, sizeof(hdr), "%g###col%d", axVal, c);
            ImGui::TableSetupColumn(hdr, ImGuiTableColumnFlags_WidthFixed, cellW);
        }

        // --- Custom header row with editable axis values ---
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers, rowHeight);

        // Top-left corner: axis labels (Y\X for 2D tables)
        ImGui::TableSetColumnIndex(0);
        if (cornerLabel[0]) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "%s", cornerLabel);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rows: %s\nColumns: %s",
                                  visualYAxis ? visualYAxis->name : "index",
                                  visualXAxis ? visualXAxis->name : "index");
        }

        // Column headers (X axis values) - editable
        for (int c = 0; c < cols; c++) {
            ImGui::TableSetColumnIndex(c + 1);

            bool isLiveCol = (c == liveCol);
            if (isLiveCol) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(255, 255, 255, 50));
            }

            // Editing this axis cell?
            if (state.editingAxis && state.editingXAxis && state.editAxisIndex == c) {
                ImGui::SetNextItemWidth(cellW - 4);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 200));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0.5f, 1));
                char axId[16];
                snprintf(axId, sizeof(axId), "##axX%d", c);
                if (ImGui::InputText(axId, state.axisEditBuf, sizeof(state.axisEditBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll)) {
                    float newVal = 0.0f;
                    bool parsed = ParseStrictFloat(state.axisEditBuf, newVal);
                    if (!parsed) {
                        strncpy(state.inputError, "Axis value must be a finite number",
                                sizeof(state.inputError) - 1);
                    } else if (IsAxisEditable(visualXAxis)) {
                        const float step = std::max(0.001f, std::fabs(visualXAxis->scale));
                        const float lower = c > 0 ? cal.readAxisValue(*visualXAxis, c - 1) + step : -1e30f;
                        const float upper = c + 1 < visualXAxis->count ? cal.readAxisValue(*visualXAxis, c + 1) - step : 1e30f;
                        if (lower > upper) {
                            strncpy(state.inputError, "Repair neighboring axis values first", sizeof(state.inputError) - 1);
                            parsed = false;
                        } else newVal = std::clamp(newVal, lower, upper);
                    }
                    if (parsed && IsAxisEditable(visualXAxis) && !AxisValueIsOrdered(*visualXAxis, cal, c, newVal)) {
                        strncpy(state.inputError, "Axis values must remain strictly increasing",
                                sizeof(state.inputError) - 1);
                    } else if (parsed && IsAxisEditable(visualXAxis)) {
                        int sz = (visualXAxis->type == CellType::U16BE || visualXAxis->type == CellType::S16BE) ? 2 : 1;
                        int off = visualXAxis->offset + c * sz;
                        undo.record(cal.data, off, sz, "Edit axis");
                        cal.writeAxisValue(*visualXAxis, c, newVal);
                        undo.finalize(cal.data, off, sz);
                        modified = true;
                        state.inputError[0] = '\0';
                        state.editingAxis = false;
                    }
                }
                float axisCandidate = 0.0f;
                DrawInvalidInputOutline(!ParseStrictFloat(state.axisEditBuf, axisCandidate) || state.inputError[0]);
                ImGui::PopStyleColor(2);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) state.editingAxis = false;
            } else {
                float axVal = GetAxisValue(visualXAxis, cal, c);
                char axTxt[32];
                snprintf(axTxt, sizeof(axTxt), "%g", axVal);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.5f, 1.0f));
                char axSelLabel[32];
                snprintf(axSelLabel, sizeof(axSelLabel), "%s##ahX%d", axTxt, c);
                if (ImGui::Selectable(axSelLabel, false, ImGuiSelectableFlags_None, ImVec2(cellW - S(4), rowHeight))) {
                    if (IsAxisEditable(visualXAxis)) {
                        state.editingAxis = true;
                        state.editingXAxis = true;
                        state.editAxisIndex = c;
                        snprintf(state.axisEditBuf, sizeof(state.axisEditBuf), "%g", axVal);
                        state.inputError[0] = '\0';
                    }
                }
                ImGui::PopStyleColor();
            }
        }

        // --- Data rows (top-to-bottom: low RPM at top, high RPM at bottom) ---
        for (int r = 0; r < rows; r++) {
            ImGui::TableNextRow(0, rowHeight);

            bool isLiveRow = (r == liveRow);

            // Row header (Y axis value) - editable
            ImGui::TableSetColumnIndex(0);

            if (isLiveRow) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(255, 255, 255, 50));
            }

            if (visualYAxis) {
                // Editing this row axis cell?
                if (state.editingAxis && !state.editingXAxis && state.editAxisIndex == r) {
                    ImGui::SetNextItemWidth(headerW - S(4));
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 200));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0.5f, 1));
                    char axId[16];
                    snprintf(axId, sizeof(axId), "##axY%d", r);
                    if (ImGui::InputText(axId, state.axisEditBuf, sizeof(state.axisEditBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue |
                                         ImGuiInputTextFlags_AutoSelectAll)) {
                        float newVal = 0.0f;
                        bool parsed = ParseStrictFloat(state.axisEditBuf, newVal);
                        if (!parsed) {
                            strncpy(state.inputError, "Axis value must be a finite number",
                                    sizeof(state.inputError) - 1);
                        } else if (IsAxisEditable(visualYAxis)) {
                            const float step = std::max(0.001f, std::fabs(visualYAxis->scale));
                            const float lower = r > 0 ? cal.readAxisValue(*visualYAxis, r - 1) + step : -1e30f;
                            const float upper = r + 1 < visualYAxis->count ? cal.readAxisValue(*visualYAxis, r + 1) - step : 1e30f;
                            if (lower > upper) {
                                strncpy(state.inputError, "Repair neighboring axis values first", sizeof(state.inputError) - 1);
                                parsed = false;
                            } else newVal = std::clamp(newVal, lower, upper);
                        }
                        if (parsed && IsAxisEditable(visualYAxis) && !AxisValueIsOrdered(*visualYAxis, cal, r, newVal)) {
                            strncpy(state.inputError, "Axis values must remain strictly increasing",
                                    sizeof(state.inputError) - 1);
                        } else if (parsed && IsAxisEditable(visualYAxis)) {
                            int sz = (visualYAxis->type == CellType::U16BE || visualYAxis->type == CellType::S16BE) ? 2 : 1;
                            int off = visualYAxis->offset + r * sz;
                            undo.record(cal.data, off, sz, "Edit axis");
                            cal.writeAxisValue(*visualYAxis, r, newVal);
                            undo.finalize(cal.data, off, sz);
                            modified = true;
                            state.inputError[0] = '\0';
                            state.editingAxis = false;
                        }
                    }
                    float axisCandidate = 0.0f;
                    DrawInvalidInputOutline(!ParseStrictFloat(state.axisEditBuf, axisCandidate) || state.inputError[0]);
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) state.editingAxis = false;
                } else {
                    float yVal = GetAxisValue(visualYAxis, cal, r);
                    char yTxt[32];
                    snprintf(yTxt, sizeof(yTxt), "%g", yVal);

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.5f, 1.0f));
                    char ySelLabel[32];
                    snprintf(ySelLabel, sizeof(ySelLabel), "%s##ahY%d", yTxt, r);
                    if (ImGui::Selectable(ySelLabel, false, ImGuiSelectableFlags_None, ImVec2(headerW - S(4), rowHeight))) {
                        if (IsAxisEditable(visualYAxis)) {
                            state.editingAxis = true;
                            state.editingXAxis = false;
                            state.editAxisIndex = r;
                            snprintf(state.axisEditBuf, sizeof(state.axisEditBuf), "%g", yVal);
                            state.inputError[0] = '\0';
                        }
                    }
                    ImGui::PopStyleColor();
                }
            } else if (!is1D) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "%d", r);
            }

            // Data cells
            for (int c = 0; c < cols; c++) {
                ImGui::TableSetColumnIndex(c + 1);

                float val = cal.readTableCell(table, r, c);
                bool selected = state.sel.contains(r, c);
                bool isLiveCell = (r == liveRow && c == liveCol);

                // Colour across the range this table actually uses, not its
                // declared limits: a 0-255 table holding 31-89 would otherwise
                // occupy a sliver of the ramp and read as one flat colour. This
                // matches the range printed in the legend below the grid.
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                       CellColor(val, heatMin, heatMax));

                // Diff triangle marker (top-right corner) if cell differs from compare data
                bool cellDiffers = false;
                if (compareData) {
                    int sz = cal.cellSize(table.cellType);
                    int off = table.transposed
                        ? table.offset + (c * table.cols + r) * sz
                        : table.offset + (r * table.cols + c) * sz;
                    int curRaw = readRawCell(cal.data, off, table.cellType);
                    int cmpRaw = readRawCell(compareData, off, table.cellType);
                    cellDiffers = (curRaw != cmpRaw);
                    if (cellDiffers) {
                        ImVec2 cp = ImGui::GetCursorScreenPos();
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        float tri = 7.0f;
                        dl->AddTriangleFilled(
                            ImVec2(cp.x + cellW - 4 - tri, cp.y),
                            ImVec2(cp.x + cellW - 4, cp.y),
                            ImVec2(cp.x + cellW - 4, cp.y + tri),
                            IM_COL32(255, 160, 0, 220));
                    }
                }

                char cellId[32];
                snprintf(cellId, sizeof(cellId), "##c%d_%d", r, c);

                // Inline editing (single cell)
                if (state.editing && state.editRow == r && state.editCol == c) {
                    ImGui::SetNextItemWidth(cellW - 4);
                    if (state.editFocusNeeded) {
                        ImGui::SetKeyboardFocusHere();
                        state.editFocusNeeded = false;
                    }
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 200));
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
                    ImGuiInputTextFlags editFlags = ImGuiInputTextFlags_EnterReturnsTrue;
                    if (state.editSelectAll) editFlags |= ImGuiInputTextFlags_AutoSelectAll;
                    const bool enterCommit = ImGui::InputText(cellId, state.editBuf, sizeof(state.editBuf), editFlags);
                    float liveCandidate = 0.0f;
                    const bool liveCandidateValid = ParseStrictFloat(state.editBuf, liveCandidate);
                    DrawInvalidInputOutline(!liveCandidateValid || liveCandidate < table.minVal || liveCandidate > table.maxVal);
                    const bool clickAwayCommit = ImGui::IsItemDeactivated();
                    const bool keyboardAdvance = enterCommit || ImGui::IsKeyPressed(ImGuiKey_Tab);
                    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
                    if (!cancelled && (enterCommit || clickAwayCommit)) {
                        float newVal = 0.0f;
                        if (ParseStrictFloat(state.editBuf, newVal)) {
                            newVal = std::clamp(newVal, table.minVal, table.maxVal);
                            int sz = cal.cellSize(table.cellType);
                            undo.record(cal.data, table.offset, rows * cols * sz, "Edit cell");
                            cal.writeTableCell(table, r, c, newVal);
                            undo.finalize(cal.data, table.offset, rows * cols * sz);
                            state.editing = false;
                            state.inputError[0] = '\0';
                            modified = true;
                            // Enter/Tab advances; Shift+Tab moves backward.
                            if (keyboardAdvance) {
                                const int direction = ImGui::GetIO().KeyShift ? -1 : 1;
                                int nextRow = r;
                                int nextCol = c + direction;
                                if (nextCol >= cols) { nextCol = 0; nextRow = std::min(rows - 1, r + 1); }
                                if (nextCol < 0) { nextCol = cols - 1; nextRow = std::max(0, r - 1); }
                                state.sel.select(nextRow, nextCol);
                            }
                        } else {
                            snprintf(state.inputError, sizeof(state.inputError), "Cell value must be a finite number");
                            state.editFocusNeeded = true;
                        }
                    }
                    ImGui::PopStyleColor(2);
                    if (cancelled) {
                        state.editing = false;
                        state.inputError[0] = '\0';
                    }
                } else {
                    // Display value
                    char txt[32];
                    if (table.scale == 1.0f && table.translate == 0.0f)
                        snprintf(txt, sizeof(txt), "%d", (int)val);
                    else if (fabsf(table.scale) >= 0.1f)
                        snprintf(txt, sizeof(txt), "%.1f", val);
                    else
                        snprintf(txt, sizeof(txt), "%.2f", val);

                    // Contrast follows the cell's own colour. Selection must not
                    // enter into it: forcing dark text on a dark cell made a
                    // selected low-value region unreadable.
                    const float normalized = heatMax > heatMin
                        ? std::clamp((val - heatMin) / (heatMax - heatMin), 0.0f, 1.0f)
                        : 0.5f;
                    ImU32 textCol = (normalized < 0.42f)
                        ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255);
                    ImGui::PushStyleColor(ImGuiCol_Text, textCol);

                    // Use ### separator: display text changes but ID stays unique per cell
                    char cellLabel[48];
                    snprintf(cellLabel, sizeof(cellLabel), "%s###c%d_%d", txt, r, c);
                    if (ImGui::Selectable(cellLabel, false,
                                          ImGuiSelectableFlags_None,
                                          ImVec2(cellW - 4, rowHeight))) {
                        if (ImGui::GetIO().KeyShift) {
                            state.sel.extendTo(r, c);
                        } else {
                            state.sel.select(r, c);
                        }
                        state.editingAxis = false;
                    }
                    ImGui::PopStyleColor();

                    const ImVec2 cellMin = ImGui::GetItemRectMin();
                    const ImVec2 cellMax = ImGui::GetItemRectMax();
                    ImDrawList* overlay = ImGui::GetWindowDrawList();
                    if (selected) {
                        // Draw only the edges that face outside the selection, so a
                        // large block gets one clean boundary instead of a grid of
                        // boxes over every cell.
                        const ImU32 selCol = IM_COL32(255, 225, 90, 255);
                        const float t = S(2.0f);
                        const CellSelection& s = state.sel;
                        if (r == s.minRow())
                            overlay->AddLine(ImVec2(cellMin.x, cellMin.y), ImVec2(cellMax.x, cellMin.y), selCol, t);
                        if (r == s.maxRow())
                            overlay->AddLine(ImVec2(cellMin.x, cellMax.y), ImVec2(cellMax.x, cellMax.y), selCol, t);
                        if (c == s.minCol())
                            overlay->AddLine(ImVec2(cellMin.x, cellMin.y), ImVec2(cellMin.x, cellMax.y), selCol, t);
                        if (c == s.maxCol())
                            overlay->AddLine(ImVec2(cellMax.x, cellMin.y), ImVec2(cellMax.x, cellMax.y), selCol, t);
                        // The anchor cell keeps its own outline so it stays findable
                        // inside a large selection.
                        if (r == s.endRow && c == s.endCol)
                            overlay->AddRect(cellMin, cellMax, IM_COL32(255, 225, 90, 160), 0.0f, 0, S(1.0f));
                    }
                    if (isLiveCell) {
                        const ImU32 cyan = IM_COL32(55, 225, 245, 255);
                        const float cx = (cellMin.x + cellMax.x) * 0.5f;
                        const float cy = (cellMin.y + cellMax.y) * 0.5f;
                        overlay->AddLine(ImVec2(cx, cellMin.y), ImVec2(cx, cellMax.y), cyan, S(1.5f));
                        overlay->AddLine(ImVec2(cellMin.x, cy), ImVec2(cellMax.x, cy), cyan, S(1.5f));
                    }

                    // Cell tooltip: raw value and memory offset
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        int sz = cal.cellSize(table.cellType);
                        int off = table.transposed
                            ? table.offset + (c * table.cols + r) * sz
                            : table.offset + (r * table.cols + c) * sz;
                        int raw = cal.readCell(off, table.cellType);
                        if (cellDiffers) {
                            int cmpRaw = readRawCell(compareData, off, table.cellType);
                            float cmpVal = (float)cmpRaw * table.scale + table.translate;
                            ImGui::SetTooltip("%g %s  |  raw %d  |  0x%03X\nwas %g (raw %d)", val, table.units, raw, off, cmpVal, cmpRaw);
                        } else if (sz == 2)
                            ImGui::SetTooltip("%g %s  |  raw %d  |  0x%03X", val, table.units, raw, off);
                        else
                            ImGui::SetTooltip("%g %s  |  raw %d (0x%02X)  |  0x%03X", val, table.units, raw, raw & 0xFF, off);
                    }

                    // Drag selection (AllowWhenBlockedByActiveItem so hover works while mouse is held)
                    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                    if (hovered && ImGui::IsMouseDown(0) && !state.sel.dragging) {
                        if (!ImGui::GetIO().KeyShift)
                            state.sel.select(r, c);
                        state.sel.dragging = true;
                    }
                    if (hovered && state.sel.dragging) {
                        state.sel.extendTo(r, c);
                    }

                    // Double-click = inline edit
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        state.editing = true;
                        state.editFocusNeeded = true;
                        state.editSelectAll = true;
                        state.editRow = r;
                        state.editCol = c;
                        snprintf(state.editBuf, sizeof(state.editBuf), "%g", val);
                    }

                    // Right-click context menu
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        if (!state.sel.contains(r, c))
                            state.sel.select(r, c);
                        ImGui::OpenPopup("CellCtx");
                    }
                }
            }
        }
        ImGui::EndTable();
    }

    // Release drag
    if (!ImGui::IsMouseDown(0)) state.sel.dragging = false;

    // Persistent marker and heatmap legend.
    ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.30f, 1.0f), "Outline: selection");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.22f, 0.86f, 0.95f, 1.0f), "  Crosshair: live");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.60f, 0.15f, 1.0f), "  Corner: compare delta");
    ImGui::SameLine();
    ImGui::TextDisabled("  Heatmap %g / %g / %g %s", actualMin,
                        (actualMin + actualMax) * 0.5f, actualMax, table.units);
    ImGui::SameLine();
    ImGui::TextDisabled("  |  Ctrl+Up/Down or keypad +/- nudges (Shift = x10)");

    // Context menu (right-click)
    if (ImGui::BeginPopup("CellCtx")) {
        if (ImGui::MenuItem("Copy", "Ctrl+C")) CopySelection(table, cal, state.sel);
        if (ImGui::MenuItem("Copy with axis labels"))
            CopySelection(table, cal, state.sel, true);
        if (ImGui::MenuItem("Paste", "Ctrl+V")) {
            const PasteOutcome outcome = PasteSelection(table, cal, undo, state.sel);
            ApplyPasteOutcome(state, outcome);
            modified |= outcome.modified;
        }
        if (ImGui::MenuItem("Paste Grid...", "Ctrl+Shift+V")) state.openPasteGrid = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Interpolate", nullptr, false, state.sel.isMulti())) {
            InterpolateSelection(table, cal, undo, state.sel); modified = true;
        }
        if (ImGui::MenuItem("Smooth", nullptr, false, state.sel.isMulti())) {
            SmoothSelection(table, cal, undo, state.sel); modified = true;
        }
        if (ImGui::MenuItem("Set to zero")) { SetSelection(table, cal, undo, state.sel, 0); modified = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Select All", "Ctrl+A")) {
            state.sel.startRow = 0; state.sel.startCol = 0;
            state.sel.endRow = rows - 1; state.sel.endCol = cols - 1;
            state.sel.active = true;
        }
        ImGui::EndPopup();
    }

    // Live cell indicator text with diagnostic info
    if (liveRow >= 0 && liveCol >= 0) {
        float liveVal = cal.readTableCell(table, liveRow, liveCol);
        // Show axis values at the matched indices
        float xAxisVal = GetAxisValue(visualXAxis, cal, liveCol);
        float yAxisVal = visualYAxis ? GetAxisValue(visualYAxis, cal, liveRow) : (float)liveRow;
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "LIVE [%s=%.0f, %s=%.0f] = %g %s (byte 0x%03X)",
                           visualXAxis ? visualXAxis->name : "X", xAxisVal,
                           visualYAxis ? visualYAxis->name : "Y", yAxisVal,
                           liveVal, table.units,
                           table.transposed
                               ? table.offset + (liveCol * table.cols + liveRow) * cal.cellSize(table.cellType)
                               : table.offset + (liveRow * table.cols + liveCol) * cal.cellSize(table.cellType));

        // For VE table: show monitor VE for comparison
        if (mon && mon->valid && table.offset == 0x000 && strcmp(table.units, "%") == 0) {
            float monVE = static_cast<float>(mon->ve);
            ImU32 matchCol = (fabsf(monVE - liveVal) < 5.0f)
                ? IM_COL32(80, 230, 80, 255) : IM_COL32(255, 200, 50, 255);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(matchCol),
                               "  ECU VE=%.1f%%  Mon KPA=%d RPM=%d",
                               monVE, mon->kpa, mon->rpm);
        }
        // For Ignition table: show monitor timing
        if (mon && mon->valid && table.offset == 0x100) {
            float monTiming = mon->advance;
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(IM_COL32(200, 200, 100, 255)),
                               "  ECU Timing=%.1f%s  Mon KPA=%d RPM=%d",
                               monTiming, "\xC2\xB0", mon->kpa, mon->rpm);
        }
    }

    // Compare diff summary
    if (compareData) {
        int diffCount = 0;
        int sz = cal.cellSize(table.cellType);
        for (int r2 = 0; r2 < rows; r2++)
            for (int c2 = 0; c2 < cols; c2++) {
                int off = table.transposed
                    ? table.offset + (c2 * table.cols + r2) * sz
                    : table.offset + (r2 * table.cols + c2) * sz;
                if (readRawCell(cal.data, off, table.cellType) != readRawCell(compareData, off, table.cellType))
                    diffCount++;
            }
        if (diffCount > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%d/%d cells differ from reference", diffCount, rows * cols);
    }

    modified |= DrawPasteGridDialog(table, cal, undo, state);

    ImGui::PopID(); // match PushID(table.offset)
    return modified;
}

// ============================================================
//  Setting dependency checks
// ============================================================

static const char* GetDisabledReason(int offset, const CalBuffer& cal) { return DisabledReason(offset, cal.data); }
static bool IsSettingEnabled(int offset, const CalBuffer& cal) { return !DisabledReason(offset, cal.data); }
// ============================================================
//  Scalar Editor
// ============================================================

void DrawScalarEditors(const char* category, CalBuffer& cal, UndoStack& undo, EcuProtocol* ecu) {
    bool hasEntries = false;
    for (int i = 0; i < NUM_SCALARS; ++i)
        if (strcmp(ALL_SCALARS[i].category, category) == 0) { hasEntries = true; break; }
    if (!hasEntries) return;
    static int invalidScalar = -1;
    static std::vector<float> pendingValues;
    static std::vector<float> baseValues;
    static std::vector<unsigned char> editingValues;
    static std::vector<int> lastSeenFrame;
    if ((int)pendingValues.size() != NUM_SCALARS) {
        pendingValues.assign(NUM_SCALARS, 0.0f);
        baseValues.assign(NUM_SCALARS, 0.0f);
        editingValues.assign(NUM_SCALARS, 0);
        lastSeenFrame.assign(NUM_SCALARS, -2);
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("scalar_form", 4, flags)) return;
    ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 1.15f);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthFixed, S(205));
    ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, S(65));
    ImGui::TableSetupColumn("Status / help", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < NUM_SCALARS; i++) {
        const ScalarDef& s = ALL_SCALARS[i];
        if (strcmp(s.category, category) != 0) continue;

        bool enabled = IsSettingEnabled(s.offset, cal);
        const char* disabledReason = enabled ? nullptr : GetDisabledReason(s.offset, cal);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(s.name);
        if (gApp.pendingSettingName[0] && strcmp(gApp.pendingSettingName, s.name) == 0) {
            ImGui::SetScrollHereY(0.35f);
            gApp.pendingSettingName[0] = '\0';
        }

        ImGui::TableNextColumn();
        if (!enabled) ImGui::BeginDisabled();

        const float oldVal = cal.readScalar(s);
        const int frame = ImGui::GetFrameCount();
        if (editingValues[i] && (lastSeenFrame[i] != frame - 1 || baseValues[i] != oldVal)) {
            editingValues[i] = 0;
            if (invalidScalar == i) invalidScalar = -1;
        }
        if (!editingValues[i]) pendingValues[i] = baseValues[i] = oldVal;
        float& val = pendingValues[i];

        ImGui::PushID(i);
        ImGui::SetNextItemWidth(S(120));
        const float absScale = std::fabs(s.scale);
        const float step = absScale > 0.001f ? absScale : 0.001f;
        const char* format = absScale >= 1.0f ? "%.0f" : absScale >= 0.1f ? "%.1f" : "%.2f";
        if (invalidScalar == i) ImGui::SetKeyboardFocusHere();
        ImGui::InputFloat("##value", &val, step, step * 10.0f, format);
        const bool scalarInvalidNow = !std::isfinite(val) || val < s.minVal || val > s.maxVal;
        DrawInvalidInputOutline(invalidScalar == i || scalarInvalidNow);
        lastSeenFrame[i] = frame;
        if (ImGui::IsItemActivated() || ImGui::IsItemActive()) {
            editingValues[i] = 1;
            baseValues[i] = oldVal;
        }
        const bool deactivated = ImGui::IsItemDeactivated();
        const bool edited = ImGui::IsItemDeactivatedAfterEdit();
        const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (cancelled && editingValues[i]) {
            pendingValues[i] = oldVal;
            editingValues[i] = 0;
            invalidScalar = -1;
        } else if (deactivated && editingValues[i]) {
            if (!edited || val == oldVal) {
                editingValues[i] = 0;
                invalidScalar = -1;
            } else if (std::isfinite(val)) {
                val = std::clamp(val, s.minVal, s.maxVal);
                undo.record(cal.data, 0, TOTAL_SIZE, s.name);
                cal.writeScalar(s, val);
                undo.finalize(cal.data, 0, TOTAL_SIZE);
                invalidScalar = -1;
                editingValues[i] = 0;
            } else {
                invalidScalar = i;
            }
        }
        // Tooltip on the drag control
        { const char* tip = GetTooltip(s.name);
          if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip); }

        // TPS Set buttons: read current ADC and set as min/max
        bool isTpsMin = (s.offset == 0x7B0);
        bool isTpsMax = (s.offset == 0x7B2);
        if ((isTpsMin || isTpsMax) && ecu && ecu->port && ecu->port->isOpen()) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.55f, 1.0f));
            char btnLabel[32];
            snprintf(btnLabel, sizeof(btnLabel), "Set %s##tps%d", isTpsMin ? "Closed" : "WOT", i);
            if (ImGui::SmallButton(btnLabel)) {
                // Pause monitor to avoid bus contention
                bool wasMonitoring = ecu->monitorActive;
                if (wasMonitoring && !ecu->stopMonitor()) return;
                Sleep(50);
                PurgeComm(ecu->port->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

                ecu->lockSerial();
                int adcVal = ecu->readTpsAdc();
                ecu->unlockSerial();
                if (adcVal >= 0) {
                    int sz = cal.cellSize(s.type);
                    undo.record(cal.data, 0, TOTAL_SIZE, s.name);
                    cal.writeScalar(s, (float)adcVal);
                    undo.finalize(cal.data, 0, TOTAL_SIZE);
                }

                if (adcVal < 0) snprintf(gApp.statusLine,sizeof(gApp.statusLine),"TPS capture unavailable: stop the engine and wait for a fresh sample and completed homing");
                if (wasMonitoring) ecu->startMonitor();
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Read current TPS ADC from ECU and set as %s",
                                  isTpsMin ? "closed throttle" : "wide open throttle");
            }
        }

        ImGui::PopID();

        if (!enabled) ImGui::EndDisabled();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(s.units && s.units[0] ? s.units : "-");
        ImGui::TableNextColumn();
        if (invalidScalar == i) {
            ImGui::TextColored(ImVec4(0.95f, 0.32f, 0.25f, 1.0f),
                               "Invalid: enter %g-%g %s", s.minVal, s.maxVal, s.units);
        } else if (!enabled && disabledReason) {
            ImGui::TextColored(ImVec4(0.78f, 0.58f, 0.30f, 1.0f), "%s", disabledReason);
        } else {
            ImGui::TextDisabled("Allowed: %g-%g %s", s.minVal, s.maxVal, s.units);
        }
        if (gApp.compareActive) {
            const int curRaw = readRawCell(cal.data, s.offset, s.type);
            const int cmpRaw = readRawCell(gApp.compareData, s.offset, s.type);
            if (curRaw != cmpRaw) {
                const float cmpVal = (float)cmpRaw * s.scale + s.translate;
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Previous: %g %s", cmpVal, s.units);
            }
        }
        const char* help = GetTooltip(s.name);
        if (help && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", help);
    }
    ImGui::EndTable();
}

void DrawFlagEditors(const char* category, CalBuffer& cal, UndoStack& undo) {
    bool hasEntries = false;
    for (int i = 0; i < NUM_FLAGS; ++i)
        if (strcmp(ALL_FLAGS[i].category, category) == 0) { hasEntries = true; break; }
    if (!hasEntries) return;
    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("flag_form", 4, flags)) return;
    ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 1.15f);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthFixed, S(205));
    ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, S(65));
    ImGui::TableSetupColumn("Status / help", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableHeadersRow();
    for (int i = 0; i < NUM_FLAGS; i++) {
        const FlagDef& f = ALL_FLAGS[i];
        if (strcmp(f.category, category) != 0) continue;

        bool enabled = IsSettingEnabled(f.offset, cal);
        const char* disabledReason = enabled ? nullptr : GetDisabledReason(f.offset, cal);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(f.name);
        if (gApp.pendingSettingName[0] && strcmp(gApp.pendingSettingName, f.name) == 0) {
            ImGui::SetScrollHereY(0.35f); gApp.pendingSettingName[0] = '\0';
        }
        ImGui::TableNextColumn();
        if (!enabled) ImGui::BeginDisabled();

        bool val = cal.readFlag(f);
        bool oldVal = val;

        ImGui::PushID(1000 + i);
        if (ImGui::Checkbox("##enabled", &val)) {
            if (val != oldVal) {
                undo.record(cal.data, f.offset, 1, f.name);
                cal.writeFlag(f, val);
                undo.finalize(cal.data, f.offset, 1);
            }
        }
        // Tooltip on the checkbox
        { const char* tip = GetTooltip(f.name);
          if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip); }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.6f, 1.0f), "%s", val ? f.onLabel : f.offLabel);

        // Compare diff indicator
        if (gApp.compareActive) {
            bool cmpVal = (gApp.compareData[f.offset] & f.mask) != 0;
            if (cmpVal != val) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Previous: %s", cmpVal ? f.onLabel : f.offLabel);
            }
        }

        ImGui::PopID();

        if (!enabled) ImGui::EndDisabled();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        const char* help = GetTooltip(f.name);
        if (!enabled && disabledReason)
            ImGui::TextColored(ImVec4(0.78f, 0.58f, 0.30f, 1.0f), "%s", disabledReason);
        else if (help) ImGui::TextWrapped("%s", help);
    }
    ImGui::EndTable();
}

// ============================================================
//  Dropdown Editor
// ============================================================

void DrawDropdownEditors(const char* category, CalBuffer& cal, UndoStack& undo) {
    bool hasEntries = false;
    for (int i = 0; i < NUM_DROPDOWNS; ++i)
        if (strcmp(ALL_DROPDOWNS[i].category, category) == 0) { hasEntries = true; break; }
    if (!hasEntries) return;
    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("dropdown_form", 4, flags)) return;
    ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 1.15f);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthFixed, S(205));
    ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, S(65));
    ImGui::TableSetupColumn("Status / help", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableHeadersRow();
    for (int i = 0; i < NUM_DROPDOWNS; i++) {
        const DropdownDef& d = ALL_DROPDOWNS[i];
        if (strcmp(d.category, category) != 0) continue;

        bool enabled = IsSettingEnabled(d.offset, cal);
        const char* disabledReason = enabled ? nullptr : GetDisabledReason(d.offset, cal);
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(d.name);
        if (gApp.pendingSettingName[0] && strcmp(gApp.pendingSettingName, d.name) == 0) {
            ImGui::SetScrollHereY(0.35f); gApp.pendingSettingName[0] = '\0';
        }
        ImGui::TableNextColumn();
        if (!enabled) ImGui::BeginDisabled();

        int val = cal.readDropdown(d);
        if (val >= d.numOptions) val = 0; // clamp erased/invalid values
        int oldVal = val;

        ImGui::PushID(2000 + i);
        ImGui::SetNextItemWidth(S(180));
        if (ImGui::Combo("##mode", &val, d.options, d.numOptions)) {
            if (val != oldVal) {
                undo.record(cal.data, 0, TOTAL_SIZE, d.name);
                cal.writeDropdown(d, val);
                undo.finalize(cal.data, 0, TOTAL_SIZE);
            }
        }
        // Tooltip on the combo
        { const char* tip = GetTooltip(d.name);
          if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip); }

        // Compare diff indicator
        if (gApp.compareActive) {
            int cmpVal = gApp.compareData[d.offset];
            if (cmpVal >= d.numOptions) cmpVal = 0;
            if (cmpVal != val) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Previous: %s", d.options[cmpVal]);
            }
        }

        ImGui::PopID();

        if (!enabled) ImGui::EndDisabled();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        const char* help = GetTooltip(d.name);
        if (!enabled && disabledReason)
            ImGui::TextColored(ImVec4(0.78f, 0.58f, 0.30f, 1.0f), "%s", disabledReason);
        else if (help) ImGui::TextWrapped("%s", help);
    }
    ImGui::EndTable();
}

// ============================================================
//  Clipboard Operations
// ============================================================

void CopySelection(const TableDef& table, const CalBuffer& cal, const CellSelection& sel,
                   bool withAxes) {
    if (!sel.active) return;

    const AxisDef* visualXAxis = table.xAxis ? &ResolveAxis(*table.xAxis, cal.data) : nullptr;
    const AxisDef* visualYAxis = table.yAxis ? &ResolveAxis(*table.yAxis, cal.data) : nullptr;
    char buf[32];

    clipboardData.clear();
    if (withAxes && visualXAxis) {
        // Leading blank keeps the header aligned above the value columns.
        if (visualYAxis) clipboardData += "\t";
        for (int c = sel.minCol(); c <= sel.maxCol(); c++) {
            snprintf(buf, sizeof(buf), "%g", GetAxisValue(visualXAxis, cal, c));
            clipboardData += buf;
            if (c < sel.maxCol()) clipboardData += "\t";
        }
        clipboardData += "\n";
    }
    for (int r = sel.minRow(); r <= sel.maxRow(); r++) {
        if (withAxes && visualYAxis) {
            snprintf(buf, sizeof(buf), "%g\t", GetAxisValue(visualYAxis, cal, r));
            clipboardData += buf;
        }
        for (int c = sel.minCol(); c <= sel.maxCol(); c++) {
            float val = cal.readTableCell(table, r, c);
            snprintf(buf, sizeof(buf), "%g", val);
            clipboardData += buf;
            if (c < sel.maxCol()) clipboardData += "\t";
        }
        if (r < sel.maxRow()) clipboardData += "\n";
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, clipboardData.size() + 1);
        if (hg) {
            memcpy(GlobalLock(hg), clipboardData.c_str(), clipboardData.size() + 1);
            GlobalUnlock(hg);
            SetClipboardData(CF_TEXT, hg);
        }
        CloseClipboard();
    }
}

// True when `values` can serve as this axis's breakpoints: the axis is stored in
// the calibration, the count matches, and the values are strictly increasing
// (the firmware's lookups assume that).
bool AxisValuesUsable(const AxisDef* axis, const std::vector<float>& values, int expected) {
    if (!axis || axis->offset < 0 || (int)values.size() != expected || expected < 1) return false;
    for (int i = 1; i < expected; ++i)
        if (!(values[i] > values[i - 1])) return false;
    return true;
}

// Writes breakpoints as one undo step. Returns false when they were unusable.
static bool ApplyAxisValues(const AxisDef* axis, const std::vector<float>& values, int expected,
                            CalBuffer& cal, UndoStack& undo) {
    if (!AxisValuesUsable(axis, values, expected)) return false;
    const int sz = (axis->type == CellType::U16BE || axis->type == CellType::S16BE) ? 2 : 1;
    undo.record(cal.data, axis->offset, expected * sz, "Paste axis");
    for (int i = 0; i < expected; ++i) cal.writeAxisValue(*axis, i, values[i]);
    undo.finalize(cal.data, axis->offset, expected * sz);
    return true;
}

static std::string ReadClipboardText() {
    std::string text;
    if (OpenClipboard(NULL)) {
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData) {
            const char* data = (const char*)GlobalLock(hData);
            if (data) text = data;
            GlobalUnlock(hData);
        }
        CloseClipboard();
    }
    return text;
}

PasteOutcome PasteGridText(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                           CellSelection& sel, const std::string& text,
                           PasteMode mode, bool applyAxes) {
    PasteOutcome outcome;

    const int visualRows = table.transposed ? table.cols : table.rows;
    const int visualCols = table.transposed ? table.rows : table.cols;

    GridPasteRequest req;
    req.targetRows = visualRows;
    req.targetCols = visualCols;
    // Add and Multiply combine with the existing cell, so the pasted numbers
    // are offsets or factors and must not be judged against the table limits.
    req.minVal = (mode == PasteMode::Replace) ? table.minVal : 0.0f;
    req.maxVal = (mode == PasteMode::Replace) ? table.maxVal : 0.0f;

    const GridPasteParse parsed = ParseGridPaste(text, req);
    if (!parsed.ok) {
        outcome.message = parsed.error.empty() ? "Nothing to paste." : parsed.error;
        return outcome;
    }

    // Where the block lands. A full-size grid always covers the whole table so a
    // 16x16 map can be dropped in without lining up the cursor first; a single
    // value fills whatever is selected.
    const bool fullTable = (parsed.rows == visualRows && parsed.cols == visualCols);
    const bool singleValue = (parsed.rows == 1 && parsed.cols == 1);
    int startRow = 0, startCol = 0;
    int spanRows = parsed.rows, spanCols = parsed.cols;
    if (singleValue && sel.active) {
        startRow = sel.minRow(); startCol = sel.minCol();
        spanRows = sel.countRows(); spanCols = sel.countCols();
    } else if (!fullTable && sel.active) {
        startRow = sel.minRow(); startCol = sel.minCol();
    }

    const int fitRows = std::min(spanRows, visualRows - startRow);
    const int fitCols = std::min(spanCols, visualCols - startCol);
    if (fitRows <= 0 || fitCols <= 0) {
        outcome.message = "Nothing was pasted — the selected cell leaves no room for the values.";
        return outcome;
    }

    const int sz = cal.cellSize(table.cellType);
    undo.record(cal.data, table.offset, table.rows * table.cols * sz, "Paste");
    for (int r = 0; r < fitRows; r++)
        for (int c = 0; c < fitCols; c++) {
            const float pasted = singleValue ? parsed.at(0, 0) : parsed.at(r, c);
            const int row = startRow + r, col = startCol + c;
            float result = pasted;
            if (mode == PasteMode::Add) result = cal.readTableCell(table, row, col) + pasted;
            else if (mode == PasteMode::Multiply) result = cal.readTableCell(table, row, col) * pasted;
            cal.writeTableCell(table, row, col, result);  // clamps to the table limits
        }
    undo.finalize(cal.data, table.offset, table.rows * table.cols * sz);

    // Axis breakpoints, when the paste carried usable ones and the user asked.
    int axesApplied = 0;
    if (applyAxes) {
        const AxisDef* xAxis = table.xAxis ? &ResolveAxis(*table.xAxis, cal.data) : nullptr;
        const AxisDef* yAxis = table.yAxis ? &ResolveAxis(*table.yAxis, cal.data) : nullptr;
        if (ApplyAxisValues(xAxis, parsed.headerRow, visualCols, cal, undo)) ++axesApplied;
        if (ApplyAxisValues(yAxis, parsed.headerCol, visualRows, cal, undo)) ++axesApplied;
    }

    sel.startRow = startRow;
    sel.startCol = startCol;
    sel.endRow = startRow + fitRows - 1;
    sel.endCol = startCol + fitCols - 1;
    sel.active = true;

    const char* verb = mode == PasteMode::Add      ? "Added"
                     : mode == PasteMode::Multiply ? "Multiplied by"
                                                   : "Pasted";
    char buf[192];
    if (fullTable)
        snprintf(buf, sizeof(buf), "%s %dx%d over the whole table.", verb, parsed.cols, parsed.rows);
    else if (singleValue)
        snprintf(buf, sizeof(buf), "%s %g %s across %d cells.", verb, parsed.at(0, 0),
                 table.units, fitRows * fitCols);
    else
        snprintf(buf, sizeof(buf), "%s %dx%d at row %d, column %d.",
                 verb, fitCols, fitRows, startRow + 1, startCol + 1);
    outcome.message = buf;
    if (axesApplied) {
        snprintf(buf, sizeof(buf), " %d axis/axes updated.", axesApplied);
        outcome.message += buf;
    }

    if (!singleValue && (fitRows < parsed.rows || fitCols < parsed.cols)) {
        snprintf(buf, sizeof(buf), " %dx%d did not fit and was dropped.",
                 parsed.cols - fitCols, parsed.rows - fitRows);
        outcome.message += buf;
    }
    if (!axesApplied && (parsed.strippedHeaderRow || parsed.strippedHeaderCol))
        outcome.message += " Axis labels ignored.";
    if (parsed.clamped) {
        snprintf(buf, sizeof(buf), " %d value(s) clamped to %g-%g %s.",
                 parsed.clamped, table.minVal, table.maxVal, table.units);
        outcome.message += buf;
    }

    outcome.ok = true;
    outcome.modified = true;
    return outcome;
}

PasteOutcome PasteSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                            CellSelection& sel) {
    std::string text = ReadClipboardText();
    if (text.empty()) text = clipboardData;
    if (text.empty()) {
        PasteOutcome outcome;
        outcome.message = "Clipboard is empty.";
        return outcome;
    }
    return PasteGridText(table, cal, undo, sel, text);
}

void MultiplySelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                        const CellSelection& sel, float factor) {
    if (!sel.active) return;
    int sz = cal.cellSize(table.cellType);
    undo.record(cal.data, table.offset, table.rows * table.cols * sz, "Multiply");
    for (int r = sel.minRow(); r <= sel.maxRow(); r++)
        for (int c = sel.minCol(); c <= sel.maxCol(); c++) {
            float v = cal.readTableCell(table, r, c);
            cal.writeTableCell(table, r, c, v * factor);
        }
    undo.finalize(cal.data, table.offset, table.rows * table.cols * sz);
}

void DivideSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                      const CellSelection& sel, float divisor) {
    if (!sel.active || divisor == 0.0f) return;
    MultiplySelection(table, cal, undo, sel, 1.0f / divisor);
}

void SmoothSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                     const CellSelection& sel) {
    if (!sel.active || sel.countCells() < 2) return;
    const int visualRows = table.transposed ? table.cols : table.rows;
    const int visualCols = table.transposed ? table.rows : table.cols;
    const int r0 = sel.minRow(), r1 = sel.maxRow();
    const int c0 = sel.minCol(), c1 = sel.maxCol();

    // Snapshot first: averaging in place would smear each result into the next.
    const int width = c1 - c0 + 1;
    std::vector<float> before((size_t)(r1 - r0 + 1) * width);
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c)
            before[(size_t)(r - r0) * width + (c - c0)] = cal.readTableCell(table, r, c);

    const int sz = cal.cellSize(table.cellType);
    undo.record(cal.data, table.offset, table.rows * table.cols * sz, "Smooth");
    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            // Neighbours are taken from the selection where possible and from the
            // live table at its edges, so smoothing never invents data.
            double total = 0.0;
            int count = 0;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    const int nr = r + dr, nc = c + dc;
                    if (nr < 0 || nr >= visualRows || nc < 0 || nc >= visualCols) continue;
                    const bool inside = (nr >= r0 && nr <= r1 && nc >= c0 && nc <= c1);
                    const float value = inside
                        ? before[(size_t)(nr - r0) * width + (nc - c0)]
                        : cal.readTableCell(table, nr, nc);
                    // Centre counts double so smoothing softens rather than flattens.
                    const int weight = (dr == 0 && dc == 0) ? 2 : 1;
                    total += value * weight;
                    count += weight;
                }
            }
            if (count) cal.writeTableCell(table, r, c, (float)(total / count));
        }
    }
    undo.finalize(cal.data, table.offset, table.rows * table.cols * sz);
}

void NudgeSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                    const CellSelection& sel, int steps) {
    if (!sel.active || steps == 0) return;
    // One raw unit is the smallest change the cell can actually store.
    const float step = (table.scale != 0.0f ? std::fabs(table.scale) : 1.0f) * (float)steps;
    AddToSelection(table, cal, undo, sel, step);
}

void AddToSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                     const CellSelection& sel, float addend) {
    if (!sel.active) return;
    int sz = cal.cellSize(table.cellType);
    undo.record(cal.data, table.offset, table.rows * table.cols * sz, "Add");
    for (int r = sel.minRow(); r <= sel.maxRow(); r++)
        for (int c = sel.minCol(); c <= sel.maxCol(); c++) {
            float v = cal.readTableCell(table, r, c);
            cal.writeTableCell(table, r, c, v + addend);
        }
    undo.finalize(cal.data, table.offset, table.rows * table.cols * sz);
}

void SetSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                   const CellSelection& sel, float value) {
    if (!sel.active) return;
    int sz = cal.cellSize(table.cellType);
    undo.record(cal.data, table.offset, table.rows * table.cols * sz, "Set");
    for (int r = sel.minRow(); r <= sel.maxRow(); r++)
        for (int c = sel.minCol(); c <= sel.maxCol(); c++)
            cal.writeTableCell(table, r, c, value);
    undo.finalize(cal.data, table.offset, table.rows * table.cols * sz);
}

void InterpolateRows(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                     const CellSelection& sel) {
    if (!sel.active || sel.countCols() < 2) return;
    const int sz = cal.cellSize(table.cellType);
    undo.record(cal.data, table.offset, table.rows * table.cols * sz, "Interpolate rows");
    for (int r = sel.minRow(); r <= sel.maxRow(); ++r) {
        const float first = cal.readTableCell(table, r, sel.minCol());
        const float last = cal.readTableCell(table, r, sel.maxCol());
        const int span = sel.maxCol() - sel.minCol();
        for (int c = sel.minCol(); c <= sel.maxCol(); ++c) {
            const float t = (float)(c - sel.minCol()) / (float)span;
            cal.writeTableCell(table, r, c, first + (last - first) * t);
        }
    }
    undo.finalize(cal.data, table.offset, table.rows * table.cols * sz);
}

void InterpolateColumns(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                        const CellSelection& sel) {
    if (!sel.active || sel.countRows() < 2) return;
    const int sz = cal.cellSize(table.cellType);
    undo.record(cal.data, table.offset, table.rows * table.cols * sz, "Interpolate columns");
    for (int c = sel.minCol(); c <= sel.maxCol(); ++c) {
        const float first = cal.readTableCell(table, sel.minRow(), c);
        const float last = cal.readTableCell(table, sel.maxRow(), c);
        const int span = sel.maxRow() - sel.minRow();
        for (int r = sel.minRow(); r <= sel.maxRow(); ++r) {
            const float t = (float)(r - sel.minRow()) / (float)span;
            cal.writeTableCell(table, r, c, first + (last - first) * t);
        }
    }
    undo.finalize(cal.data, table.offset, table.rows * table.cols * sz);
}

void InterpolateSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                            const CellSelection& sel) {
    if (!sel.active) return;
    int r0 = sel.minRow(), r1 = sel.maxRow();
    int c0 = sel.minCol(), c1 = sel.maxCol();

    int sz = cal.cellSize(table.cellType);
    undo.record(cal.data, table.offset, table.rows * table.cols * sz, "Interpolate");

    float tl = cal.readTableCell(table, r1, c0);
    float tr = cal.readTableCell(table, r1, c1);
    float bl = cal.readTableCell(table, r0, c0);
    float br = cal.readTableCell(table, r0, c1);

    int dr = r1 - r0;
    int dc = c1 - c0;

    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            float ty = (dr > 0) ? (float)(r - r0) / (float)dr : 0.0f;
            float tx = (dc > 0) ? (float)(c - c0) / (float)dc : 0.0f;

            float top = tl + (tr - tl) * tx;
            float bot = bl + (br - bl) * tx;
            float val = bot + (top - bot) * ty;
            cal.writeTableCell(table, r, c, val);
        }
    }

    undo.finalize(cal.data, table.offset, table.rows * table.cols * sz);
}
