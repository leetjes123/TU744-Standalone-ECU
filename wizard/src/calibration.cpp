#include "calibration.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace {
void RawLimits(CellType type, int& minimum, int& maximum) {
    switch (type) {
        case CellType::U8: minimum = 0; maximum = 255; break;
        case CellType::S8: minimum = -128; maximum = 127; break;
        case CellType::U16BE: minimum = 0; maximum = 65535; break;
        case CellType::S16BE: minimum = -32768; maximum = 32767; break;
    }
}

bool QuantizeRaw(float display, float scale, float translate, CellType type, int& raw) {
    if (!std::isfinite(display) || !std::isfinite(scale) || scale == 0.0f || !std::isfinite(translate)) return false;
    int minimum = 0, maximum = 0;
    RawLimits(type, minimum, maximum);
    double normalized = ((double)display - translate) / scale;
    if (!std::isfinite(normalized)) return false;
    normalized = std::clamp(normalized, (double)minimum, (double)maximum);
    raw = (int)std::lround(normalized);
    raw = std::clamp(raw, minimum, maximum);
    return true;
}
}
#include <cmath>
#include <io.h>
#include <windows.h>

// ============================================================
//  Cell helpers
// ============================================================

int CalBuffer::readCell(int offset, CellType t) const {
    switch (t) {
        case CellType::U8:    return readU8(offset);
        case CellType::S8:    return readS8(offset);
        case CellType::U16BE: return readU16BE(offset);
        case CellType::S16BE: return readS16BE(offset);
    }
    return 0;
}

void CalBuffer::writeCell(int offset, CellType t, int val) {
    switch (t) {
        case CellType::U8:    writeU8(offset, val); break;
        case CellType::S8:    writeS8(offset, val); break;
        case CellType::U16BE: writeU16BE(offset, val); break;
        case CellType::S16BE: writeS16BE(offset, val); break;
    }
}

int CalBuffer::cellSize(CellType t) const {
    return (t == CellType::U16BE || t == CellType::S16BE) ? 2 : 1;
}

float CalBuffer::readTableCell(const TableDef& table, int row, int col) const {
    int sz = cellSize(table.cellType);
    // When transposed, visual row=dataCol, visual col=dataRow
    int off = table.transposed
        ? table.offset + (col * table.cols + row) * sz
        : table.offset + (row * table.cols + col) * sz;
    int raw = readCell(off, table.cellType);
    return raw * table.scale + table.translate;
}

void CalBuffer::writeTableCell(const TableDef& table, int row, int col, float displayVal) {
    int sz = cellSize(table.cellType);
    int off = table.transposed
        ? table.offset + (col * table.cols + row) * sz
        : table.offset + (row * table.cols + col) * sz;
    if (!std::isfinite(displayVal)) return;
    displayVal = std::clamp(displayVal, table.minVal, table.maxVal);
    int raw = 0;
    if (!QuantizeRaw(displayVal, table.scale, table.translate, table.cellType, raw)) return;
    writeCell(off, table.cellType, raw);
    recomputeDirty();
}

float CalBuffer::readAxisValue(const AxisDef& requested, int index) const {
    const auto& axis = ResolveAxis(requested, data);
    if (index < 0 || index >= axis.count) return 0;
    if (axis.offset < 0) return axis.fixed ? axis.fixed[index] : float(index);

    int sz = (axis.type == CellType::U16BE || axis.type == CellType::S16BE) ? 2 : 1;
    int off = axis.offset + index * sz;
    int raw = readCell(off, axis.type);
    return raw * axis.scale + axis.translate;
}

void CalBuffer::writeAxisValue(const AxisDef& requested, int index, float displayVal) {
    const auto& axis = ResolveAxis(requested, data);
    if (axis.offset < 0 || index < 0 || index >= axis.count) return;

    int sz = (axis.type == CellType::U16BE || axis.type == CellType::S16BE) ? 2 : 1;
    int off = axis.offset + index * sz;
    int raw = 0;
    if (!QuantizeRaw(displayVal, axis.scale, axis.translate, axis.type, raw)) return;
    writeCell(off, axis.type, raw);
    recomputeDirty();
}

float CalBuffer::readScalar(const ScalarDef& s) const {
    int raw = readCell(s.offset, s.type);
    return raw * s.scale + s.translate;
}

void CalBuffer::writeScalar(const ScalarDef& s, float displayVal) {
    if (!std::isfinite(displayVal)) return;
    displayVal = std::clamp(displayVal, s.minVal, s.maxVal);
    int raw = 0;
    if (!QuantizeRaw(displayVal, s.scale, s.translate, s.type, raw)) return;
    writeCell(s.offset, s.type, raw);
    recomputeDirty();
}

bool CalBuffer::axisValueIsOrdered(const AxisDef& axis, int index, float displayVal) const {
    int raw = 0;
    if (!QuantizeRaw(displayVal, axis.scale, axis.translate, axis.type, raw)) return false;
    const float quantized = raw * axis.scale + axis.translate;
    return (index == 0 || quantized > readAxisValue(axis, index - 1)) &&
           (index + 1 >= axis.count || quantized < readAxisValue(axis, index + 1));
}

bool CalBuffer::writeAxisValueOrdered(const AxisDef& axis, int index, float displayVal) {
    if (!axisValueIsOrdered(axis, index, displayVal)) return false;
    writeAxisValue(axis, index, displayVal);
    return true;
}

void CalBuffer::recomputeDirty() {
    dirty = memcmp(data, savedData, TOTAL_SIZE) != 0;
}

bool CalBuffer::readFlag(const FlagDef& f) const {
    return (data[f.offset] & f.mask) != 0;
}

void CalBuffer::writeFlag(const FlagDef& f, bool val) {
    if (val) data[f.offset] |= f.mask;
    else     data[f.offset] &= ~f.mask;
    recomputeDirty();
}

int CalBuffer::readDropdown(const DropdownDef& d) const {
    return (data[d.offset] & d.mask) >> d.shift;
}

void CalBuffer::writeDropdown(const DropdownDef& d, int val) {
    data[d.offset] = (data[d.offset] & ~d.mask) | ((val << d.shift) & d.mask);
    recomputeDirty();
}

bool CalBuffer::loadFromFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    unsigned char staged[TOTAL_SIZE] = {};
    if (sz != CAL_SIZE || fread(staged, 1, CAL_SIZE, f) != CAL_SIZE) { fclose(f); return false; }
    fclose(f);
    if (!HasSchemaMarker(staged)) return false;
    memcpy(data, staged, TOTAL_SIZE);
    memcpy(savedData, data, TOTAL_SIZE);
    loaded = true;
    dirty = false;
    strncpy(filePath, path, sizeof(filePath) - 1);
    filePath[sizeof(filePath) - 1] = '\0';
    return true;
}

bool CalBuffer::saveToFile(const char* path) {
    if (!path || !path[0] || !loaded || !HasSchemaMarker(data)) return false;

    // Never truncate the last known-good calibration. Write and flush a sibling
    // temporary file, then atomically replace the destination.
    char directory[MAX_PATH] = ".";
    strncpy(directory, path, sizeof(directory) - 1);
    directory[sizeof(directory) - 1] = '\0';
    char* slash = strrchr(directory, '\\');
    char* altSlash = strrchr(directory, '/');
    if (!slash || (altSlash && altSlash > slash)) slash = altSlash;
    if (slash) {
        if (slash == directory + 2 && directory[1] == ':') slash[1] = '\0';
        else *slash = '\0';
    } else strcpy(directory, ".");
    char tempPath[MAX_PATH] = {};
    if (!GetTempFileNameA(directory, "tw", 0, tempPath)) return false;

    FILE* f = fopen(tempPath, "wb");
    if (!f) { DeleteFileA(tempPath); return false; }
    unsigned char saveBuf[TOTAL_SIZE];
    memcpy(saveBuf, data, TOTAL_SIZE);
    const bool wrote = fwrite(saveBuf, 1, TOTAL_SIZE, f) == TOTAL_SIZE;
    const bool flushed = wrote && fflush(f) == 0 && _commit(_fileno(f)) == 0;
    const bool closed = fclose(f) == 0;
    if (!wrote || !flushed || !closed) {
        DeleteFileA(tempPath);
        return false;
    }
    if (!MoveFileExA(tempPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tempPath);
        return false;
    }
    memcpy(savedData, data, TOTAL_SIZE);
    dirty = false;
    strncpy(filePath, path, sizeof(filePath) - 1);
    filePath[sizeof(filePath) - 1] = '\0';
    return true;
}


bool HasSchemaMarker(const unsigned char* data) {
    return data && data[0x900]=='L' && data[0x901]=='R' && data[0x902]==0 && data[0x903]==5;
}
const AxisDef& ResolveAxis(const AxisDef& axis, const unsigned char* data) {
    return axis.offset == -2 ? ((data[0x5D4] & 1) ? AXIS_TPS : AXIS_KPA) : axis;
}
const char* DisabledReason(int offset, const unsigned char* data) {
    for (int i=0; i<NUM_DEPENDENCIES; ++i) {
        const auto& d=DEPENDENCIES[i];
        if (d.offset==offset && (data[d.control]&d.mask)!=d.value) return d.reason;
    }
    return nullptr;
}
