#include "tooltips.h"
#include <cstring>
const char* GetTooltip(const char* name) {
    for(int i=0;i<NUM_TABLES;++i) if(!strcmp(name,ALL_TABLES[i].name)) return ALL_TABLES[i].description;
    for(int i=0;i<NUM_SCALARS;++i) if(!strcmp(name,ALL_SCALARS[i].name)) return ALL_SCALARS[i].description;
    for(int i=0;i<NUM_FLAGS;++i) if(!strcmp(name,ALL_FLAGS[i].name)) return ALL_FLAGS[i].description;
    for(int i=0;i<NUM_DROPDOWNS;++i) if(!strcmp(name,ALL_DROPDOWNS[i].name)) return ALL_DROPDOWNS[i].description;
    return nullptr;
}
const char* GetCategoryHelp(const char*) { return "Edit locally, write to the active tune, then save to ECU flash with the engine stopped."; }
