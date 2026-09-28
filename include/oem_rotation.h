#ifndef LRE_OEM_ROTATION_H
#define LRE_OEM_ROTATION_H
#include "ecu.h"
/* Native diagnostic input contract, not the standalone output decoder.
   Register fields record the OEM's requested peripheral effects; this API
   never writes standalone SFRs. Native capture units and call sites must be
   preserved by the eventual board observer. See OEM-DIAGNOSTICS.md. */
typedef struct {
    u16 capture_low, capture_previous, period_previous, period_older, period;
    u16 threshold, rotation_flags, flags12, flags14, flags08, flags66, speed_word;
    u16 t0, pecc2, srcp2, dstp2, xp1ic, phase_begin, phase_end;
    u16 reset_f7a6, reset_f8d4, reset_9bb6, history_low, history_period, history_filter;
    u8 capture_high, previous_high, capture_count, equipment, phase_index, next_phase;
    u8 active, tooth_count, phase_match, skipped, reset_8ae0, reset_9501;
    u8 reset_f8d0, reset_f8d3, speed, speed_fast, history_high, history_period_high;
    u8 speed_filtered;
} OemRotation;
void oem_rotation_threshold(OemRotation *s);
void oem_rotation_reset(OemRotation *s);
void oem_rotation_capture(OemRotation *s);
void oem_rotation_period(OemRotation *s);
void oem_rotation_speed(OemRotation *s);
#endif
