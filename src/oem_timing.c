#include "oem_timing.h"
/* 37E04..37E28,37F5C..37F80,386C2..386E6: leave at least two
   substeps for the timer stage by borrowing a whole tooth when possible. */
void oem_angle_split(u16 counts, OemAngleStage *out) {
    out->teeth = counts >> 3;
    out->fraction = counts & 7U;
    if (out->teeth && out->fraction < 2U) {
        out->teeth--;
        out->fraction += 8U;
    }
}
/* 38944..38970 / 38984..389CE / 38B1E..38B4A.
   Preserve the ROM's saturating multiply and modular subtraction. */
u16 oem_angle_refine(u16 period, u16 fraction, u16 correction, u8 dwell) {
    u16 unit = period >> 3, result;
    u32 product = (u32)unit * fraction;
    result = product > 65535UL ? 65535U : (u16)product;
    if (dwell) {
        result = (u16)(result - correction);
        if (result < (u16)(unit * 2U)) result = (u16)(unit * 2U);
    }
    return result;
}
static s16 signed_limit(s32 n) {
    if (n > 32767L) return 32767;
    if (n < -32768L) return (s16)-32768L;
    return (s16)n;
}
static u16 unsigned_limit(s32 n) {
    if (n > 65535L) return 65535U;
    if (n < 0) return 0;
    return (u16)n;
}
/* Arithmetic/state portion of 38BDC..38D5C. The caller supplies the
   image-specific eligibility and captured interval; no guessed CC9 values. */
void oem_dwell_update(OemDwell *s, u16 base, u16 measured,
                      u8 missing, u8 fallback, u8 disabled,
                      u16 rise, u16 fall, s16 ceiling, u8 gain) {
    s16 proposed, lower, correction;
    u16 duration;
    u32 scaled;
    if (disabled) { proposed = 0; duration = base; }
    else if (fallback || s->previous_fallback) {
        proposed = s->correction;
        duration = unsigned_limit((s32)base + proposed);
    } else if (missing) {
        proposed = signed_limit((s32)s->correction + rise);
        duration = unsigned_limit((s32)base + proposed);
    } else {
        scaled = gain ? ((u32)measured << 8) / gain : 65535UL;
        duration = scaled > 65535UL ? 65535U : (u16)scaled;
        proposed = signed_limit((s32)duration - base);
    }
    if (ceiling < proposed && (u16)ceiling < 32767U) {
        correction = ceiling;
        duration = unsigned_limit((s32)base + (u16)ceiling);
    } else {
        lower = signed_limit((s32)s->correction - fall);
        if (lower < proposed) correction = proposed;
        else {
            correction = lower;
            /* ROM uses the preceding correction for this duration. */
            duration = unsigned_limit((s32)base + s->correction);
        }
    }
    if (duration < 125U) { duration = 125U; correction = 0; }
    s->duration = duration;
    s->correction = correction;
    s->previous_fallback = fallback;
}
void oem_dwell_stock_update(OemDwell *state, u16 base, u16 measured,
                            u8 missing, u8 fallback, u8 disabled) {
    /* TU5JP 14E00/14DFC/14DFE/10DD6, not shared across engine images. */
    oem_dwell_update(state, base, measured, missing, fallback, disabled,
                     63, 125, 1875, 251);
}
