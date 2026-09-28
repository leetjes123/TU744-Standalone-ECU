#pragma once

// Reference help: how the ECU builds its outputs, what every map and unit
// means, and how this application moves calibration around.

struct HelpBook {
    bool active = false;
    int  topic  = 0;
    char search[64] = {};

    void open()  { active = true; }
    void close() { active = false; search[0] = '\0'; }
};

// Draw the help window. Returns true while it is open.
bool DrawHelpBook(HelpBook& help);
