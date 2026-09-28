#include "oem.h"
#include "oem_input_data.h"

static u8 byte_cap(u16 value) {
    return value > 255U ? 255U : (u8)value;
}
static u16 ratio(u16 a, u16 b, u16 divisor) {
    u32 result;
    if (!divisor)
        return 65535U;
    result = (u32)a * b / divisor;
    return result > 65535UL ? 65535U : (u16)result;
}
static u16 filter_byte(u16 state, u8 target, u16 coefficient) {
    s32 delta = (s32)target * 256L - state;
    u32 step, magnitude;
    if (!delta)
        return state;
    magnitude = (u32)(delta < 0 ? -delta : delta);
    step = ((u32)coefficient * magnitude) >> 16;
    if (!step)
        step = 1;
    return (u16)clamp32((s32)state + (delta < 0 ? -(s32)step : (s32)step), 0, 65535L);
}

/* 0x69740..0x6997C: native IAT event0x5B, including substitution/filtering.
   adc is published byte9208, not a voltage or a Celsius value. The update
   returns the descriptor immediately before the OEM ingestion call. */
void oem_iat_init(OemIat *s) {
    if (s->startup_flags & 0x8000U)
        s->captured = OEM_IAT_FALLBACK;
    s->pass_count = s->fail_count = OEM_IAT_DELAY;
    s->raw = iat_raw[s->adc];
    s->filtered = (s->descriptor & 1U) ? OEM_IAT_FALLBACK : s->raw;
    s->filter = (u16)((s->filter & 255U) | ((u16)s->filtered << 8));
}
void oem_iat_reset(OemIat *s) {
    if (s->descriptor & 128U) {
        s->pass_count = s->fail_count = OEM_IAT_DELAY;
        s->status &= 0xFFFBU;
    }
}
u16 oem_iat_update(OemIat *s) {
    u16 descriptor = s->descriptor;
    u8 target;
    s->raw = iat_raw[s->adc];
    if (s->raw > OEM_IAT_HIGH) {
        descriptor = (u16)((descriptor & 0xF0FFU) | 0x0100U);
        s->status |= 16U;
    } else
        s->status &= 0xFFEFU;
    s->status |= 2U;
    if (s->raw < OEM_IAT_LOW) {
        descriptor = (u16)((descriptor & 0xF0FFU) | 0x0200U);
        s->status |= 32U;
    } else
        s->status &= 0xFFDFU;
    if (!(s->status & 16U) && s->raw >= OEM_IAT_LOW) {
        if (s->pass_count)
            s->pass_count--;
        else
            s->status |= 8U;
    } else {
        s->status &= 0xFFF7U;
        s->pass_count = OEM_IAT_DELAY;
    }
    if (s->status & 48U) {
        if (s->fail_count)
            s->fail_count--;
        else
            s->status |= 4U;
    } else {
        s->status &= 0xFFFBU;
        s->fail_count = OEM_IAT_DELAY;
    }
    if (s->status & 8U)
        descriptor &= 0xF0BEU;
    else if (s->status & 4U)
        descriptor |= 65U;
    if (s->status & 12U)
        descriptor |= 0x2002U;
    target = s->raw;
    if (descriptor & 1U)
        target = (!(s->coolant_descriptor & 1U) && s->coolant <= OEM_IAT_COOLANT_MAX)
                     ? s->coolant
                     : OEM_IAT_FALLBACK;
    s->filter = filter_byte(s->filter, target, OEM_IAT_FILTER);
    s->filtered = (u8)(s->filter >> 8);
    return descriptor;
}
void oem_iat_capture(OemIat *s) {
    if (s->run_flags & 4U)
        s->captured = s->filtered;
}

/* Native diagnostic operating state; separate from tunable standalone
   cranking policy. 0x329C8/0x329D6 use strictly >/< comparisons, and the
   first qualified rotation call enters crank without testing speed. */
void oem_engine_init(OemEngineState *s) {
    s->run_flags &= 0xFFF9U;
    s->count_a = s->running_count = 0;
}
void oem_engine_update(OemEngineState *s) {
    if ((s->run_flags & 4U) && s->running_count < 65535U)
        s->running_count++;
    if (!(s->run_flags & 6U)) {
        if (s->rotation_flags & 64U)
            s->run_flags |= 2U;
        return;
    }
    if (s->speed > crank_upper[s->coolant]) {
        if (s->run_flags & 2U)
            s->run_flags = (u16)((s->run_flags | 4U) & 0xFFFDU);
    } else if (s->speed < crank_lower[s->iat] && !(s->run_flags & 2U))
        s->run_flags = (u16)((s->run_flags | 2U) & 0xFFFBU);
}

/* Identical conversion bodies 0x6B760 and0x6B8BA. Unsaturated shifts at
   95F2/95F6 deliberately wrap to a byte; their neighbours saturate. */
void oem_context_update(OemContext *s) {
    u16 load = ratio(255U, s->value[0], s->value[1]);
    s->output[5] = byte_cap(ratio(load, s->value[2], OEM_CONTEXT_SPEED_SCALE));
    s->output[10] = (u8)(s->value[3] >> 2);
    s->output[12] = (u8)(s->value[4] >> 2);
    s->output[11] = byte_cap(s->value[5] >> 2);
    s->output[8] = byte_cap(s->value[6] >> 4);
    s->output[9] = byte_cap(s->value[7] >> 4);
    s->output[4] = byte_cap(s->value[8] >> 5);
    s->output[7] = byte_cap(s->value[9] >> 2);
    s->output[3] = byte_cap(s->value[10] >> 5);
    s->output[1] = (u8)(s->value[11] >> 8);
    s->output[0] = (u8)(s->value[12] >> 8);
    s->output[2] = (u8)(s->value[13] >> 8);
    s->output[6] = s->coolant <= 11U ? 0 : (u8)(((u16)s->coolant * 3U >> 2) - 8U);
    s->output[13] = s->vehicle_speed >= 204U ? 255U : (u8)((u16)s->vehicle_speed * 5U / 4U);
}
