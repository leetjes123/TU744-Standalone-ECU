#include "oem.h"
#include "oem_vss_data.h"

static u16 quotient(u32 numerator, u16 denominator) {
    u32 value;
    if (!denominator)
        return 65535U;
    value = numerator / denominator;
    return value > 65535UL ? 65535U : (u16)value;
}
static s16 difference(u16 a, u16 b) {
    s32 value = (s32)a - b;
    return value > 32767L ? 32767 : value < -32768L ? (s16)-32768L : (s16)value;
}
static void filter(u16 *fraction, u16 *high, u16 target, u16 coefficient) {
    u32 value = ((u32)*high << 16) | *fraction;
    u32 step;
    u16 distance;
    /* 06CAE, preserving whole-word multiplication and fractional borrow. */
    if (target <= *high) {
        if (target != *high || *fraction) {
            step = (u32)(*high - target) * coefficient;
            if (!step)
                step = 1;
            value = value < step ? 0 : value - step;
        }
    } else {
        distance = (u16)(target - *high - (*fraction ? 1U : 0U));
        step = (u32)distance * coefficient;
        if (!step)
            step = 1;
        value = 0xFFFFFFFFUL - value < step ? 0xFFFFFFFFUL : value + step;
    }
    *fraction = (u16)value;
    *high = (u16)(value >> 16);
}

void oem_vss_input_init(OemVssInput *s) {
    /* 29C64: unmentioned retained RAM is deliberately not zeroed here. */
    s->fd08 |= 60U;
    s->pulse_total = 0;
    s->ccm3 = (u16)((s->ccm3 & 0xF0FFU) | 0x0A00U);
    s->next_batch = s->captured_batch = s->active_batch = 1;
    s->batch = 1;
    s->pecc5 = 1;
    s->srcp5 = s->dstp5 = 0xFE9CU;
    s->cc14ic = 0x007DU;
    s->source_count = OEM_VSS_COUNT;
}

void oem_vss_input_capture(OemVssInput *s) {
    /* Payload29FC2..2A016. The original ISR reads T1 (FE44), not CC14,
       after a PEC batch. Register-bank save/restore is outside this contract. */
    s->previous_capture = s->capture;
    s->capture = s->timer;
    s->captured_batch = s->active_batch;
    s->pecc5 = s->next_batch;
    s->active_batch = s->next_batch;
    s->fd08 &= 0xFFDFU;
    if ((s->fd00 & 256U) && (s->fd06 & 256U) && !(s->descriptor & 1U)) {
        s->pulse_total = (u16)(s->pulse_total + s->next_batch);
        if (s->pulse_total >= 0x8000U)
            s->pulse_total -= 0x8000U;
    }
}

void oem_vss_input_update(OemVssInput *s) {
    u16 value, interval;
    u8 count;
    u32 numerator;
    s16 change;
    /* Complete divider5 entry29CCC..29FAC. Source and physical branches
       intentionally retain different fields and use different filters. */
    if (!(s->fd06 & 256U)) {
        if (s->fd08 & 2U)
            s->source_speed = OEM_VSS_FALLBACK;
        else {
            value = s->source == 2U ? s->source_b : s->source_a;
            s->source_target = value < 51200U ? (u16)(((u32)value * 32UL) / 25UL) : 65535U;
            if (s->status & 8U) {
                s->source_fraction = 0;
                s->source_filter = s->source_target;
            } else
                filter(&s->source_fraction, &s->source_filter, s->source_target,
                       OEM_VSS_SOURCE_FILTER);
            s->source_speed = s->source_filter;
        }
        if (s->fd08 & 2U)
            s->status |= 8U;
        else
            s->status &= 0xFFF7U;
        s->speed = s->source_speed;
        if (s->acceleration_status < 2U && s->source == 1U) {
            change = (s16)(difference(s->acceleration_input, 175U) * 3);
            /* ASHR rounds negative values down, unlike C signed division. */
            change = change < 0 ? (s16) - ((-change + 7) / 8) : (s16)(change / 8);
            s->acceleration = (u8)change;
        } else
            s->acceleration = 0;
        s->distance = 0;
    } else {
        if (s->fd08 & 32U) {
            if (s->stale_count >= 16U) {
                s->target = 0;
                s->fd08 |= 4U;
                s->next_batch = 1;
            } else
                s->stale_count++;
        } else {
            if (s->stale_count < 15U) {
                s->period = (u16)(s->capture - s->previous_capture);
                count = s->captured_batch ? s->captured_batch : 1U;
                interval = quotient(s->period, count);
                s->batch = quotient(7812UL, interval);
                if (s->batch > OEM_VSS_MAX_BATCH)
                    s->batch = OEM_VSS_MAX_BATCH;
                if (!s->batch)
                    s->batch = 1;
                s->next_batch = (u8)s->batch;
                numerator = ((0x10C388D0UL / OEM_VSS_PULSE_SCALE) * count) << 7;
                s->numerator_low = (u16)numerator;
                s->numerator_high = (u16)(numerator >> 16);
                s->target = quotient(numerator, s->period);
                s->fd08 &= 0xFFFBU;
            }
            s->fd08 |= 32U;
            s->stale_count = 0;
        }
        filter(&s->fraction, &s->filter_high, s->target, OEM_VSS_PHYSICAL_FILTER);
        s->speed = s->physical_speed = s->filter_high;
        if (s->fd08 & 4U)
            s->acceleration = 0;
        else {
            change = (s16)(difference(s->physical_speed, s->previous_speed) / 5);
            s->acceleration = (u8)(change > 127 ? 127 : change < -128 ? -128 : change);
        }
        s->previous_speed = s->physical_speed;
        if (s->pecc5 > 254U)
            s->pecc5 = 1;
        if (s->fd00 & 256U) {
            numerator = (u32)s->pulse_total * 10000UL;
            s->numerator_low = (u16)numerator;
            s->numerator_high = (u16)(numerator >> 16);
            s->distance = quotient(numerator, OEM_VSS_PULSE_SCALE);
        } else
            s->distance = 0;
    }
    value = s->speed / 160U;
    s->vehicle_speed = value > 255U ? 255U : (u8)value;
}
