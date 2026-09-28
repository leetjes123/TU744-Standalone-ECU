#include "undo.h"
#include <algorithm>

void UndoStack::record(unsigned char* calData, int offset, int size, const char* desc) {
    // Truncate any redo history
    entries.resize(position);
    constexpr size_t MAX_UNDO_ENTRIES = 128;
    if (entries.size() >= MAX_UNDO_ENTRIES) {
        entries.erase(entries.begin());
        position = std::max(0, position - 1);
    }

    UndoEntry e;
    e.offset = offset;
    e.size = std::min(size, (int)sizeof(e.oldData));
    memcpy(e.oldData, calData + offset, e.size);
    memset(e.newData, 0, sizeof(e.newData));
    e.description = desc;
    entries.push_back(e);
    // Don't increment position yet - finalize will do it
}

void UndoStack::finalize(unsigned char* calData, int offset, int size) {
    if (entries.empty() || position >= (int)entries.size()) return;
    auto& e = entries[position];
    int sz = std::min(size, (int)sizeof(e.newData));
    memcpy(e.newData, calData + offset, sz);
    position++;
}

void UndoStack::recordChange(unsigned char* calData, int offset, int size, const char* desc,
                              unsigned char* newData) {
    entries.resize(position);
    constexpr size_t MAX_UNDO_ENTRIES = 128;
    if (entries.size() >= MAX_UNDO_ENTRIES) {
        entries.erase(entries.begin());
        position = std::max(0, position - 1);
    }

    UndoEntry e;
    e.offset = offset;
    e.size = std::min(size, (int)sizeof(e.oldData));
    memcpy(e.oldData, calData + offset, e.size);
    memcpy(calData + offset, newData, e.size);
    memcpy(e.newData, newData, e.size);
    e.description = desc;
    entries.push_back(e);
    position++;
}

void UndoStack::undo(unsigned char* calData) {
    if (!canUndo()) return;
    position--;
    auto& e = entries[position];
    memcpy(calData + e.offset, e.oldData, e.size);
}

void UndoStack::redo(unsigned char* calData) {
    if (!canRedo()) return;
    auto& e = entries[position];
    memcpy(calData + e.offset, e.newData, e.size);
    position++;
}
