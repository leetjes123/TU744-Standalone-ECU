#pragma once
#include <string>
#include <vector>

// Parsing of pasted tabular text into a numeric grid.
//
// Deliberately free of ImGui/Win32 dependencies so it can be unit tested. The
// parser is forgiving about how the text was produced (Excel, TunerPro, a log,
// a forum post) but never guesses about ambiguous data: anything it cannot read
// as a complete rectangular block of numbers is reported as an error instead of
// being partially applied.

struct GridPasteRequest {
    int   targetRows = 0;    // visual rows of the destination table (0 = unknown)
    int   targetCols = 0;    // visual columns of the destination table
    float minVal = 0.0f;     // table display limits; values outside are clamped
    float maxVal = 0.0f;
};

struct GridPasteParse {
    bool               ok = false;
    std::vector<float> values;          // row-major, rows * cols entries
    int                rows = 0;
    int                cols = 0;
    bool               strippedHeaderRow = false;
    bool               strippedHeaderCol = false;
    // Stripped axis labels, kept when they were numeric so they can be applied
    // to the table's breakpoints. Empty when the labels were text.
    std::vector<float> headerRow;       // one per grid column
    std::vector<float> headerCol;       // one per grid row
    int                clamped = 0;     // values pulled back to the table limits
    char               delimiter = 0;   // '\t', ';', ',' or ' '
    std::string        error;           // populated only when ok == false

    float at(int row, int col) const {
        return values[static_cast<size_t>(row) * cols + col];
    }
};

// Parse clipboard/typed text into a grid sized for `req`.
GridPasteParse ParseGridPaste(const std::string& text, const GridPasteRequest& req);
