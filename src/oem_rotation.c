#include "oem_rotation.h"

/* 06BE8: unsigned quotient, with the native overflow/zero-divisor result. */
static u16 quotient(u32 numerator, u16 denominator) {
    u32 value;
    if (!denominator)
        return 65535U;
    value = numerator / denominator;
    return value > 65535UL ? 65535U : (u16)value;
}
void oem_rotation_threshold(OemRotation *s) {
    /* 68548; immutable TU5JP word at ROM162EE = 100. */
    s->threshold = quotient(0x4C4B4UL, 100U);
}
void oem_rotation_reset(OemRotation *s) {
    /* 6860A. Keep the incoming bit5 and the ordering of active-byte use. */
    s->srcp2 = 0xFE9EU;
    s->dstp2 = 0xF8DAU;
    if (s->rotation_flags & 32U) {
        s->pecc2 = 255;
        s->rotation_flags &= 0xD7FFU;
    } else
        s->pecc2 = 0;
    s->t0 = 0;
    s->reset_8ae0 = 0;
    s->reset_f7a6 = 0;
    s->reset_f8d4 = 0;
    s->capture_count = 0;
    s->period_older = s->period_previous = s->period = 65535U;
    s->rotation_flags &= 0xEFFFU;
    if (s->active)
        s->rotation_flags &= 0xFDFFU;
    s->rotation_flags = (u16)((s->rotation_flags & 0xFFF9U) | ((s->flags12 >> 6) & 4U));
    s->reset_9501 = 0;
    s->rotation_flags &= 0xFBFFU;
    s->reset_f8d0 = 0;
    s->phase_match = 0;
    s->reset_9bb6 = 0;
    s->reset_f8d3 = 0;
    s->active = 0;
}
void oem_rotation_capture(OemRotation *s) {
    u32 elapsed;
    u16 previous, half, delta;
    /* 688BC; count wraps. The first capture does not alter period history. */
    s->capture_count++;
    if (s->capture_count == 1U) {
        s->capture_previous = s->capture_low;
        s->previous_high = s->capture_high;
        s->rotation_flags |= 0x0800U;
        s->t0 = 0;
        return;
    }
    s->period_older = s->period_previous;
    s->period_previous = s->period;
    elapsed = (((u32)s->capture_high << 16) | s->capture_low) -
              (((u32)s->previous_high << 16) | s->capture_previous);
    elapsed = (elapsed & 0xFFFFFFUL) >> 4;
    s->period = elapsed > 65535UL ? 65535U : (u16)elapsed;
    previous = s->capture_previous;
    s->capture_previous = s->capture_low;
    s->previous_high = s->capture_high;
    if (s->period_previous > s->threshold && s->period > s->threshold)
        s->rotation_flags = (u16)((s->rotation_flags & 0xFFBFU) | 32U);
    else if (s->period_older < s->threshold)
        s->rotation_flags = (u16)((s->rotation_flags | 64U) & 0xFFDFU);
    /* Complete 669F0 call, including its retained-value branch. */
    if ((s->period >> 1) < s->period_previous)
        s->speed_word = quotient(0x4C4B4UL, s->period);
    half = s->period_previous >> 1;
    if ((s->rotation_flags & 64U) && half > s->period && half > s->period_older) {
        s->rotation_flags |= 0x8202U;
        if (s->active)
            s->tooth_count--;
        if (s->flags14 & 16U)
            s->phase_match = (u8)((s->flags12 & 0x1000U) ? 0 : 2);
        else if (s->equipment & 1U) {
            delta = (u16)((u16)s->phase_index * 30U + 30U - s->tooth_count);
            if ((s16)delta < 30)
                s->phase_match = 0;
            else {
                /* The native increment is byte-sized before multiplication. */
                delta = (u16)((u16)(u8)(s->phase_index + 1U) * 30U - s->tooth_count);
                s->phase_match = (u8)((s16)delta > 90 ? 0 : 2);
            }
        } else
            s->phase_match = 0;
        s->flags08 &= 0xFFBFU;
        s->phase_begin = previous;
        s->phase_end = s->capture_low;
        s->pecc2 = 0x021EU;
        s->dstp2 = 0xF8DAU;
        s->xp1ic |= 128U;
    } else if (s->equipment & 1U) {
        if (s->tooth_count >= 100U) {
            s->skipped = (u8)(s->tooth_count - 100U);
            s->tooth_count = 30;
            s->xp1ic |= 128U;
        } else {
            s->tooth_count--;
            if (!s->tooth_count) {
                s->tooth_count = 30;
                s->phase_index = s->next_phase;
                s->xp1ic |= 128U;
            }
        }
    }
}
void oem_rotation_period(OemRotation *s) {
    u32 capture, previous, elapsed, product, filtered;
    /* 668B2: full-period speed and asymmetric byte filter. */
    capture = ((u32)s->capture_high << 16) | s->capture_low;
    if (!(s->flags66 & 32U)) {
        product = (u32)s->period * 480UL;
        if (product > 0xFFFFFFUL)
            product = 0xFFFFFFUL;
        previous = (capture - product) & 0xFFFFFFUL;
        s->history_low = (u16)previous;
        s->history_high = (u8)(previous >> 16);
    }
    previous = ((u32)s->history_high << 16) | s->history_low;
    s->history_low = s->capture_low;
    s->history_high = s->capture_high;
    elapsed = (capture - previous) & 0xFFFFFFUL;
    s->history_period = (u16)elapsed;
    s->history_period_high = (u8)(elapsed >> 16);
    s->flags66 = (u16)((s->flags66 & 0xFFDFU) | ((s->rotation_flags << 4) & 32U));
    if (s->rotation_flags & 2U)
        s->speed_word = s->history_period_high ? quotient(0x8F0D1UL, (u16)(elapsed >> 8))
                                               : quotient(0x8F0D180UL, s->history_period);
    if (s->speed_fast >= 150U && s->history_filter == 0x9600U)
        return;
    if ((s->history_filter >> 8) < s->speed_fast)
        filtered = ((u32)s->history_filter * 7UL + ((u32)s->speed_fast << 8)) >> 3;
    else
        filtered = ((u32)s->history_filter * 3UL + ((u32)s->speed_fast << 8)) >> 2;
    s->history_filter = filtered > 0x9600UL ? 0x9600U : (u16)filtered;
    s->speed_filtered = (u8)(s->history_filter >> 8);
}
void oem_rotation_speed(OemRotation *s) {
    u16 scaled;
    /* 66A10: native scaling and stopped-state publication. */
    if (s->rotation_flags & 32U) {
        s->speed_fast = 0;
        s->speed_word = 0;
        s->speed = 0;
        return;
    }
    scaled = s->speed_word / 160U;
    s->speed = scaled > 255U ? 255U : (u8)scaled;
    s->speed_fast = s->speed_word < 0x2800U ? (u8)(s->speed_word / 40U) : 255U;
}
