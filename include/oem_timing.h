#ifndef LRE_OEM_TIMING_H
#define LRE_OEM_TIMING_H
#include "ecu.h"
typedef struct { u16 teeth, fraction; } OemAngleStage;
typedef struct { s16 correction; u16 duration; u8 previous_fallback; } OemDwell;
/* TU5JP timing primitives. Counts are 1/8 tooth = 0.75 crank degrees.
   Source ROM and supplied functional evidence: docs/OEM-SCHEDULER.md. */
void oem_angle_split(u16 counts, OemAngleStage *out);
u16 oem_angle_refine(u16 period, u16 fraction, u16 correction, u8 dwell);
void oem_dwell_update(OemDwell *state, u16 base, u16 measured,
                      u8 missing, u8 fallback, u8 disabled,
                      u16 rise, u16 fall, s16 ceiling, u8 gain);
void oem_dwell_stock_update(OemDwell *state, u16 base, u16 measured,
                            u8 missing, u8 fallback, u8 disabled);
#endif
