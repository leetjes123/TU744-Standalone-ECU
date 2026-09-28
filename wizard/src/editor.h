#pragma once
#include "calibration.h"
#include "undo.h"
#include "protocol.h"
#include "imgui.h"
#include <string>
#include <vector>

// Selection state for a table editor
struct CellSelection {
    int startRow = -1, startCol = -1;
    int endRow = -1,   endCol = -1;
    bool active = false;
    bool dragging = false;

    void clear() { startRow = startCol = endRow = endCol = -1; active = false; dragging = false; }
    void select(int r, int c) { startRow = endRow = r; startCol = endCol = c; active = true; }
    void extendTo(int r, int c) { endRow = r; endCol = c; }

    int minRow() const { return startRow < endRow ? startRow : endRow; }
    int maxRow() const { return startRow > endRow ? startRow : endRow; }
    int minCol() const { return startCol < endCol ? startCol : endCol; }
    int maxCol() const { return startCol > endCol ? startCol : endCol; }
    int countRows() const { return active ? (maxRow() - minRow() + 1) : 0; }
    int countCols() const { return active ? (maxCol() - minCol() + 1) : 0; }
    int countCells() const { return countRows() * countCols(); }
    bool isMulti() const { return countCells() > 1; }
    bool contains(int r, int c) const {
        return active && r >= minRow() && r <= maxRow() && c >= minCol() && c <= maxCol();
    }
};

// How pasted values combine with what is already in the table.
enum class PasteMode { Replace, Add, Multiply };

// State for one open table editor tab
struct TableEditorState {
    int            tableIndex = -1;
    CellSelection  sel;
    bool           editing = false;
    bool           editFocusNeeded = false;  // set focus to InputText next frame
    bool           editSelectAll = false;    // auto-select text (Enter/F2/dblclick)
    char           editBuf[32] = {};
    int            editRow = -1, editCol = -1;

    // Axis editing
    bool           editingAxis = false;
    bool           editingXAxis = true;   // true=X(col), false=Y(row)
    int            editAxisIndex = -1;
    char           axisEditBuf[32] = {};

    // Toolbar value input
    char           toolbarValue[32] = {};
    char           inputError[128] = {};

    // Paste feedback and the "Paste Grid..." dialog
    char           pasteStatus[192] = {};
    bool           pasteStatusIsError = false;
    bool           openPasteGrid = false;      // request to open the dialog
    std::vector<char> pasteGridText;           // editable buffer for the dialog
    PasteMode      pasteMode = PasteMode::Replace;
    bool           pasteApplyAxes = false;
};

// Fixed voltage axis values (4V to 18V in 2V steps)
static const float VOLTAGE_AXIS[8] = { 4, 6, 8, 10, 12, 14, 16, 18 };

// Fixed MAP ADC axis
static const float MAP_ADC_AXIS[16] = { 0, 68, 136, 204, 273, 341, 409, 477, 546, 614, 682, 750, 819, 887, 955, 1023 };

// Heatmap color for cell value
ImU32 CellColor(float val, float minVal, float maxVal);

// Draw a table editor. Returns true if calibration was modified.
// compareData: if non-null, cells differing from this reference are marked.
bool DrawTableEditor(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                     TableEditorState& state, const MonitorData* mon,
                     const unsigned char* compareData = nullptr);

// Draw scalar editors for a given category (ecu optional, for TPS Set buttons)
void DrawScalarEditors(const char* category, CalBuffer& cal, UndoStack& undo, EcuProtocol* ecu = nullptr);

// Draw flag toggles for a given category
void DrawFlagEditors(const char* category, CalBuffer& cal, UndoStack& undo);

// Draw dropdown selectors for a given category
void DrawDropdownEditors(const char* category, CalBuffer& cal, UndoStack& undo);

// Clipboard operations
// `withAxes` prefixes the block with the X breakpoints and each row with its Y
// breakpoint, which pastes straight back in (the labels are recognised again).
void CopySelection(const TableDef& table, const CalBuffer& cal, const CellSelection& sel,
                   bool withAxes = false);

// Result of a grid paste, suitable for showing to the user verbatim.
struct PasteOutcome {
    bool        ok = false;
    bool        modified = false;
    std::string message;
};

// Paste `text` into the table. A block matching the table's full size fills the
// whole table wherever the cursor is; a single value fills the whole selection;
// anything else lands at the selection anchor (top-left when nothing is
// selected) and is clipped to the table. `sel` ends up covering what was
// written, so the result is visible immediately.
PasteOutcome PasteGridText(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                           CellSelection& sel, const std::string& text,
                           PasteMode mode = PasteMode::Replace, bool applyAxes = false);

// Same, taking the text from the Windows clipboard.
PasteOutcome PasteSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo, CellSelection& sel);

// Bulk operations on selection
void MultiplySelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                       const CellSelection& sel, float factor);
void DivideSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                     const CellSelection& sel, float divisor);
void AddToSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                    const CellSelection& sel, float addend);
void InterpolateSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                          const CellSelection& sel);
void InterpolateRows(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                     const CellSelection& sel);
void InterpolateColumns(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                        const CellSelection& sel);
// 3x3 neighbourhood average across the selection (3-tap on 1D tables). Reads
// the pre-smooth values so the pass is not biased by its own output.
void SmoothSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                     const CellSelection& sel);
// Nudge every selected cell by `steps` raw units (the smallest change the cell
// can store), which keeps the increment meaningful whatever the table's scale.
void NudgeSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                    const CellSelection& sel, int steps);
void SetSelection(const TableDef& table, CalBuffer& cal, UndoStack& undo,
                  const CellSelection& sel, float value);
