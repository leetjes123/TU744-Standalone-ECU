#include "oem.h"
#include "oem_voltage_data.h"

static void scale(OemVoltage *s) {
    /* The native shifted product is narrowed, not saturated. */
    s->scaled = (u16)(((u32)s->voltage * 1130UL) >> 2);
    s->scaled_byte = (u8)(s->scaled >> 8);
}
void oem_voltage_filter(OemVoltage *s) {
    u32 value, step;
    u16 distance;
    /* 6670C and 06CAE. Only the whole-word distance is multiplied. A
       nonzero fraction borrows one word on the rising branch. */
    scale(s);
    value = ((u32)s->filter_high << 16) | s->fraction;
    if (s->scaled <= s->filter_high) {
        if (s->scaled != s->filter_high || s->fraction) {
            step = (u32)(s->filter_high - s->scaled) * 0x3333UL;
            if (!step)
                step = 1;
            value = value < step ? 0 : value - step;
        }
    } else {
        distance = (u16)(s->scaled - s->filter_high - (s->fraction ? 1U : 0U));
        step = (u32)distance * 0x3333UL;
        if (!step)
            step = 1;
        value = 0xFFFFFFFFUL - value < step ? 0xFFFFFFFFUL : value + step;
    }
    s->fraction = (u16)value;
    s->filter_high = (u16)(value >> 16);
    s->filtered = s->filter_high;
    s->filtered_byte = (u8)(s->filtered >> 8);
}
void oem_voltage_alternate(OemVoltage *s) {
    /* 6675E does not change fault flags or the filter. */
    s->voltage = s->adc < OEM_VOLTAGE_INVALID ? OEM_VOLTAGE_FALLBACK : s->adc;
}
void oem_voltage_init(OemVoltage *s) {
    /* 663C0 leaves the fraction, status and divider as supplied. */
    s->delay = OEM_VOLTAGE_DELAY;
    s->fail_count = s->pass_count = OEM_VOLTAGE_COUNT;
    oem_voltage_alternate(s);
    scale(s);
    s->filter_high = s->filtered = s->scaled;
    s->filtered_byte = s->scaled_byte;
}
void oem_voltage_reset(OemVoltage *s) {
    if (s->descriptor & 128U)
        s->fail_count = s->pass_count = OEM_VOLTAGE_COUNT;
}
void oem_voltage_base(OemVoltage *s) {
    /* 66422; the running path deliberately retains every field. */
    if (s->run_flags & 4U)
        return;
    oem_voltage_alternate(s);
    if (s->adc < OEM_VOLTAGE_INVALID)
        s->status |= 4U;
    else
        s->status &= 0xFFFBU;
    oem_voltage_filter(s);
}
u16 oem_voltage_update(OemVoltage *s) {
    u16 descriptor = s->descriptor;
    /* 664A2..666FC, before native event65 ingestion. */
    if (s->run_flags & 4U) {
        s->status &= 0xFFE0U;
        if (s->adc < OEM_VOLTAGE_INVALID) {
            descriptor = (u16)((descriptor & 0xF0FFU) | 0x0840U);
            s->status |= 5U;
            s->voltage = OEM_VOLTAGE_FALLBACK;
        } else {
            s->voltage = s->adc;
            descriptor &= 0xFFBFU;
            if (!(descriptor & 1U))
                descriptor &= 0xF0FFU;
            if (s->adc < OEM_VOLTAGE_LOW)
                s->status |= 1U;
            else if (s->adc > OEM_VOLTAGE_HIGH)
                s->status |= 2U;
        }
        oem_voltage_filter(s);
        if (!s->delay) {
            descriptor |= 0x2002U;
            if ((s->status & 2U) && s->vehicle_speed > 0U && !(s->speed_descriptor & 1U)) {
                descriptor = (u16)((descriptor & 0xF0FFU) | 0x0100U);
                s->status |= 16U;
            } else if ((s->status & 1U) && !(s->status & 4U)) {
                descriptor = (u16)((descriptor & 0xF0FFU) | 0x0200U);
                s->status |= 8U;
            }
        }
        if ((descriptor & 0x0F00U) && !s->fail_count)
            descriptor |= 0x2003U;
        if (!(s->status & 3U)) {
            if (!s->pass_count)
                descriptor &= 0xF0FEU;
            else
                s->pass_count--;
        } else
            s->pass_count = OEM_VOLTAGE_COUNT;
        if (s->delay) {
            s->divider++;
            if (s->divider >= 10U) {
                s->divider = 0;
                s->delay--;
            }
        }
    } else {
        s->delay = OEM_VOLTAGE_DELAY;
        if (s->status & 4U) {
            descriptor = (u16)((descriptor & 0xF0FFU) | 0x0840U);
            if (!s->fail_count)
                descriptor |= 0x2003U;
        } else {
            descriptor &= 0xFFBFU;
            if (!(descriptor & 1U))
                descriptor &= 0xF0FFU;
            if (s->adc <= OEM_VOLTAGE_HIGH && s->adc >= OEM_VOLTAGE_LOW) {
                if (!s->pass_count)
                    descriptor &= 0xF0FEU;
                else
                    s->pass_count--;
            } else
                s->pass_count = OEM_VOLTAGE_COUNT;
        }
    }
    if (s->status & 28U) {
        if (s->fail_count)
            s->fail_count--;
    } else
        s->fail_count = OEM_VOLTAGE_COUNT;
    return descriptor;
}
