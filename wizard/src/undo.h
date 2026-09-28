#pragma once
#include "calibration.h"
#include <vector>
#include <cstring>

struct UndoEntry {
    int           offset;   // byte offset in cal buffer
    int           size;     // number of bytes
    // Full-buffer capacity keeps multi-setting edits atomic in undo history.
    unsigned char oldData[TOTAL_SIZE];
    unsigned char newData[TOTAL_SIZE];
    const char*   description;
};

struct UndoStack {
    std::vector<UndoEntry> entries;
    int                    position = 0;  // next entry to be written (also = undo cursor)

    void clear() { entries.clear(); position = 0; }

    // Record a change. Call BEFORE modifying the buffer.
    void record(unsigned char* calData, int offset, int size, const char* desc);

    // Finalize a previously recorded entry by capturing the new state.
    // Call AFTER modifying the buffer.
    void finalize(unsigned char* calData, int offset, int size);

    // Combined: snapshot old, execute change, snapshot new
    void recordChange(unsigned char* calData, int offset, int size, const char* desc,
                      unsigned char* newData);

    bool canUndo() const { return position > 0; }
    bool canRedo() const { return position < (int)entries.size(); }

    void undo(unsigned char* calData);
    void redo(unsigned char* calData);
};
