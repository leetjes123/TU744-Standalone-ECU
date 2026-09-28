#pragma once
#include <cstring>
#include <cstdint>

#define CAL_SIZE 3072
#define TOTAL_SIZE CAL_SIZE

// Data types for table cells
enum class CellType {
    U8,     // unsigned 8-bit
    S8,     // signed 8-bit
    U16BE,  // unsigned 16-bit big-endian
    S16BE,  // signed 16-bit big-endian
};

// Axis definition
struct AxisDef {
    const char* name;
    int         offset;    // byte offset in cal buffer
    int         count;     // number of entries
    CellType    type;      // U16BE or S16BE
    const char* units;
    float       scale;     // multiply raw by this for display
    float       translate; // add this after scaling
    const float* fixed = nullptr;
};

// Table definition (2D grid)
struct TableDef {
    const char*    name;
    const char*    category;
    int            offset;     // byte offset in cal buffer
    int            rows;       // number of DATA rows (firmware major index)
    int            cols;       // number of DATA columns (firmware minor index)
    CellType       cellType;
    const char*    units;
    float          scale;      // display = raw * scale + translate
    float          translate;
    const AxisDef* xAxis;      // visual column axis
    const AxisDef* yAxis;      // visual row axis, NULL for 1D
    float          minVal;     // display min for color scale
    float          maxVal;     // display max for color scale
    bool           transposed; // true: visual rows=dataCols, visual cols=dataRows
    const char* description = "";
};

// Scalar definition
struct ScalarDef {
    const char* name;
    const char* category;
    int         offset;
    CellType    type;
    const char* units;
    float       scale;
    float       translate;
    float       minVal;
    float       maxVal;
    const char* description = "";
};

// Bitfield flag definition
struct FlagDef {
    const char*   name;
    const char*   category;
    int           offset;
    unsigned char mask;
    const char*   offLabel;
    const char*   onLabel;
    const char* description = "";
};

// Dropdown (multi-option) definition - for masked bitfields or full-byte enums
struct DropdownDef {
    const char*         name;
    const char*         category;
    int                 offset;     // byte offset in cal buffer
    unsigned char       mask;       // bitmask to extract (e.g. 0x06 for bits 1-2)
    int                 shift;      // right-shift after masking
    const char* const*  options;    // option label array
    int                 numOptions;
    const char* description = "";
};

// -------- Axis Definitions --------
extern const AxisDef AXIS_RPM;
extern const AxisDef AXIS_KPA;
extern const AxisDef AXIS_TPS;
extern const AxisDef AXIS_TEMP;
extern const AxisDef AXIS_VOLTAGE;   // Fixed 4-18V, 8 points
extern const AxisDef AXIS_IDLE_IGN_ERR;
extern const AxisDef AXIS_ACCEL_RATE_X;
extern const AxisDef AXIS_ACCEL_RATE_Y;
extern const AxisDef AXIS_ACCEL_AMOUNT_RPM;
extern const AxisDef AXIS_MAP_ADC;   // Fixed ADC breakpoints for MAP cal
extern const AxisDef AXIS_LOAD;
const AxisDef& ResolveAxis(const AxisDef& axis, const unsigned char* data);
struct Dependency { int offset, control, mask, value; const char* reason; };
extern const Dependency DEPENDENCIES[];
extern const int NUM_DEPENDENCIES;
const char* DisabledReason(int offset, const unsigned char* data);
bool HasSchemaMarker(const unsigned char* data);

// -------- Table registry --------
extern const TableDef* ALL_TABLES;
extern const int       NUM_TABLES;

// -------- Scalar registry --------
extern const ScalarDef* ALL_SCALARS;
extern const int        NUM_SCALARS;

// -------- Flag registry --------
extern const FlagDef* ALL_FLAGS;
extern const int      NUM_FLAGS;

// -------- Dropdown registry --------
extern const DropdownDef* ALL_DROPDOWNS;
extern const int          NUM_DROPDOWNS;

// -------- Calibration buffer helpers --------
struct CalBuffer {
    unsigned char data[TOTAL_SIZE];
    unsigned char savedData[TOTAL_SIZE]; // last saved/read state for diff
    bool          loaded = false;
    bool          dirty  = false;
    char          filePath[260] = {};

    void clear()  { memset(data, 0xFF, TOTAL_SIZE); loaded = false; dirty = false; }
    void markClean() { memcpy(savedData, data, TOTAL_SIZE); dirty = false; }
    void markDirty() { dirty = true; }
    void recomputeDirty();

    // Read a value from the buffer
    int    readU8(int offset) const { return data[offset]; }
    int    readS8(int offset) const { return (signed char)data[offset]; }
    int    readU16BE(int offset) const { return (data[offset] << 8) | data[offset + 1]; }
    int    readS16BE(int offset) const { return (int16_t)((data[offset] << 8) | data[offset + 1]); }

    void   writeU8(int offset, int val)    { data[offset] = (unsigned char)(val & 0xFF); dirty = true; }
    void   writeS8(int offset, int val)    { data[offset] = (unsigned char)(val & 0xFF); dirty = true; }
    void   writeU16BE(int offset, int val) { data[offset] = (val >> 8) & 0xFF; data[offset + 1] = val & 0xFF; dirty = true; }
    void   writeS16BE(int offset, int val) { data[offset] = (val >> 8) & 0xFF; data[offset + 1] = val & 0xFF; dirty = true; }

    // Generic read/write using CellType
    int  readCell(int offset, CellType t) const;
    void writeCell(int offset, CellType t, int val);
    int  cellSize(CellType t) const;

    // Read display value from a table cell
    float readTableCell(const TableDef& table, int row, int col) const;
    void  writeTableCell(const TableDef& table, int row, int col, float displayVal);

    // Read axis value
    float readAxisValue(const AxisDef& axis, int index) const;
    void  writeAxisValue(const AxisDef& axis, int index, float displayVal);
    bool  writeAxisValueOrdered(const AxisDef& axis, int index, float displayVal);
    bool  axisValueIsOrdered(const AxisDef& axis, int index, float displayVal) const;

    // Read/write scalar
    float readScalar(const ScalarDef& s) const;
    void  writeScalar(const ScalarDef& s, float displayVal);

    // Read/write flag
    bool readFlag(const FlagDef& f) const;
    void writeFlag(const FlagDef& f, bool val);

    // Read/write dropdown (masked bitfield or full-byte enum)
    int  readDropdown(const DropdownDef& d) const;
    void writeDropdown(const DropdownDef& d, int val);


    // File I/O
    bool loadFromFile(const char* path);
    bool saveToFile(const char* path);
};
