#include "grid_paste.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

std::string Trim(const std::string& s) {
    size_t begin = 0, end = s.size();
    while (begin < end && std::isspace((unsigned char)s[begin])) ++begin;
    while (end > begin && std::isspace((unsigned char)s[end - 1])) --end;
    return s.substr(begin, end - begin);
}

// Strict: the whole field must be one finite number. Accepts a decimal comma
// ("12,5") only when the comma is not the field separator, which is the common
// case for spreadsheets exported under a European locale.
bool ParseField(const std::string& field, char delimiter, float& value) {
    std::string text = Trim(field);
    if (text.empty()) return false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        errno = 0;
        char* end = nullptr;
        const float parsed = strtof(text.c_str(), &end);
        if (end != text.c_str() && errno != ERANGE && std::isfinite(parsed) && *end == '\0') {
            value = parsed;
            return true;
        }
        if (attempt == 1 || delimiter == ',') return false;
        if (std::count(text.begin(), text.end(), ',') != 1) return false;
        std::replace(text.begin(), text.end(), ',', '.');
    }
    return false;
}

char DetectDelimiter(const std::string& text) {
    if (text.find('\t') != std::string::npos) return '\t';
    if (text.find(';') != std::string::npos) return ';';
    if (text.find(',') != std::string::npos) {
        // A lone comma per line is a decimal separator, not a column break.
        // Treat commas as delimiters only when a line holds more than one.
        size_t lineStart = 0;
        while (lineStart < text.size()) {
            size_t lineEnd = text.find_first_of("\r\n", lineStart);
            if (lineEnd == std::string::npos) lineEnd = text.size();
            if (std::count(text.begin() + lineStart, text.begin() + lineEnd, ',') > 1) return ',';
            lineStart = lineEnd;
            while (lineStart < text.size() && (text[lineStart] == '\r' || text[lineStart] == '\n'))
                ++lineStart;
        }
    }
    return ' ';
}

std::vector<std::string> SplitRow(const std::string& line, char delimiter) {
    std::vector<std::string> fields;
    if (delimiter == ' ') {
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
            size_t start = i;
            while (i < line.size() && !std::isspace((unsigned char)line[i])) ++i;
            if (i > start) fields.push_back(line.substr(start, i - start));
        }
        return fields;
    }
    size_t start = 0;
    for (;;) {
        size_t pos = line.find(delimiter, start);
        if (pos == std::string::npos) {
            fields.push_back(Trim(line.substr(start)));
            break;
        }
        fields.push_back(Trim(line.substr(start, pos - start)));
        start = pos + 1;
    }
    // Spreadsheets often end a row with a trailing separator.
    while (fields.size() > 1 && fields.back().empty()) fields.pop_back();
    return fields;
}

std::string Format(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}

} // namespace

GridPasteParse ParseGridPaste(const std::string& text, const GridPasteRequest& req) {
    GridPasteParse out;

    const char delimiter = DetectDelimiter(text);
    out.delimiter = delimiter;

    std::vector<std::vector<std::string>> grid;
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        std::vector<std::string> fields = SplitRow(text.substr(lineStart, lineEnd - lineStart), delimiter);
        bool blank = true;
        for (const std::string& f : fields)
            if (!f.empty()) { blank = false; break; }
        if (!blank) grid.push_back(std::move(fields));
        lineStart = lineEnd;
        while (lineStart < text.size() && (text[lineStart] == '\r' || text[lineStart] == '\n')) ++lineStart;
    }

    if (grid.empty()) {
        out.error = "Nothing to paste — the text holds no values.";
        return out;
    }

    auto isNumber = [&](const std::string& field) {
        float ignored = 0.0f;
        return ParseField(field, delimiter, ignored);
    };

    // Axis labels are dropped when the leading row/column is clearly a header
    // (non-numeric), or when the block is exactly one row/column larger than the
    // destination table — which is what copying a map with its axes produces.
    auto firstRowIsHeader = [&]() {
        if (grid.size() < 2) return false;
        for (size_t c = 0; c < grid[0].size(); ++c)
            if (isNumber(grid[0][c])) return false;
        return true;
    };
    auto firstColIsHeader = [&]() {
        for (const std::vector<std::string>& row : grid) {
            if (row.size() < 2) return false;
            if (isNumber(row[0])) return false;
        }
        return true;
    };

    // Keep stripped labels when they are numeric - they are axis breakpoints,
    // and the caller may want to apply them.
    auto takeRow = [&]() {
        out.headerRow.clear();
        for (size_t c = 0; c < grid[0].size(); ++c) {
            float value = 0.0f;
            if (ParseField(grid[0][c], delimiter, value)) {
                out.headerRow.push_back(value);
            } else if (c == 0) {
                continue;   // corner label such as "kPa\RPM"
            } else {
                out.headerRow.clear();
                break;
            }
        }
        grid.erase(grid.begin());
        out.strippedHeaderRow = true;
    };
    auto takeCol = [&]() {
        out.headerCol.clear();
        bool numeric = true;
        for (std::vector<std::string>& row : grid) {
            float value = 0.0f;
            if (numeric && !ParseField(row[0], delimiter, value)) numeric = false;
            if (numeric) out.headerCol.push_back(value);
            row.erase(row.begin());
        }
        if (!numeric) out.headerCol.clear();
        out.strippedHeaderCol = true;
    };

    if (firstRowIsHeader()) takeRow();
    if (!grid.empty() && firstColIsHeader()) takeCol();
    if (grid.empty() || grid[0].empty()) {
        out.error = "Nothing to paste — only header labels were found.";
        return out;
    }

    if (req.targetRows > 0 && !out.strippedHeaderRow && (int)grid.size() == req.targetRows + 1)
        takeRow();
    if (req.targetCols > 0 && !out.strippedHeaderCol && !grid.empty() &&
        (int)grid[0].size() == req.targetCols + 1) {
        bool uniform = true;
        for (const std::vector<std::string>& row : grid)
            if ((int)row.size() != req.targetCols + 1) { uniform = false; break; }
        if (uniform) takeCol();
    }
    if (grid.empty() || grid[0].empty()) {
        out.error = "Nothing to paste — only header labels were found.";
        return out;
    }

    const int rows = (int)grid.size();
    const int cols = (int)grid[0].size();
    for (int r = 1; r < rows; ++r) {
        if ((int)grid[r].size() != cols) {
            out.error = Format("Rows are not the same length — row 1 has %d values, row %d has %d.",
                               cols, r + 1, (int)grid[r].size());
            return out;
        }
    }

    out.values.resize((size_t)rows * cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            float value = 0.0f;
            if (!ParseField(grid[r][c], delimiter, value)) {
                const std::string shown = Trim(grid[r][c]);
                out.error = Format("Row %d, column %d: \"%.24s\" is not a number.",
                                   r + 1, c + 1, shown.empty() ? "(empty)" : shown.c_str());
                out.values.clear();
                return out;
            }
            if (req.maxVal > req.minVal) {
                const float limited = std::min(req.maxVal, std::max(req.minVal, value));
                if (limited != value) { ++out.clamped; value = limited; }
            }
            out.values[(size_t)r * cols + c] = value;
        }
    }

    // A header row captured before the header column was removed still carries
    // the corner cell. Anything that does not line up exactly is not usable as
    // axis breakpoints, so drop it rather than offer a misaligned axis.
    if (out.headerRow.size() == (size_t)cols + 1) out.headerRow.erase(out.headerRow.begin());
    if (out.headerRow.size() != (size_t)cols) out.headerRow.clear();
    if (out.headerCol.size() != (size_t)rows) out.headerCol.clear();

    out.rows = rows;
    out.cols = cols;
    out.ok = true;
    return out;
}
