#include "oem.h"

void oem_digital_init(OemDigital *s) {
    /* 72696: retained bits start with the stock variant-dependent seed. */
    s->published = s->older = s->newer = s->sample = (s->fd04 & 16U) ? 1U : 5U;
}
void oem_digital_filter(OemDigital *s) {
    u16 changed, value;
    /* 726CE..72792. Only the seven sampled bits are overwritten; native
       history and publication arithmetic still operates on the full word. */
    value = (u16)((s->sample & 0xFF80U) | ((s->p4 >> 4) & 1U) | ((s->p6 >> 2) & 2U) |
                  (s->p5 & 12U) | (s->p8 & 16U) | ((s->p5 << 1) & 32U) | (s->p8 & 64U));
    changed = (u16)((s->older ^ s->newer) | (s->newer ^ value));
    s->older = s->newer;
    s->newer = s->sample = value;
    s->published = (u16)((changed & s->published) | ((u16)~changed & value));
}
void oem_digital_publish(OemDigital *s) {
    /* Complete2B8E8. Keep the source's polarity and pulse-state branches;
       their electrical/vehicle interpretation is not inferred here. */
    if (s->published & 4U)
        s->fd0a &= 0xFFF7U;
    else
        s->fd0a |= 8U;
    if (s->fd2e & 0x2000U)
        s->dp2 |= 128U;
    else
        s->dp2 &= 0xFF7FU;
    if (s->fd2c & 2U)
        s->dp2 |= 64U;
    else
        s->dp2 &= 0xFFBFU;
    if (s->fd2c & 4U)
        s->retained_byte = s->source_byte;
    if (s->published & 1U)
        s->fd08 |= 0x2000U;
    else
        s->fd08 &= 0xDFFFU;
    if (s->published & 2U) {
        if (!s->pulse_count || s->pulse_state == 3U) {
            s->pulse_state = 0;
            s->fd0a &= 0xFFFEU;
            s->fd08 |= 0x4000U;
            s->pulse_count = 3;
            if (!(s->stamp_flags & 8U)) {
                /* 012C8 clock read, including a pending T1 overflow. The
                   caller supplies one coherent clock-register snapshot. */
                s->stamp_low = s->clock_low;
                s->stamp_high = (u16)(s->clock_high + ((s->clock_irq & 128U) ? 1U : 0U));
                s->stamp_flags |= 8U;
            }
        } else {
            s->pulse_state = 1;
            s->fd0a &= 0xFFFEU;
            s->fd08 |= 0x4000U;
            s->pulse_count = 3;
        }
    } else if (s->pulse_count) {
        if (!(s->fd08 & 0x4000U)) {
            s->pulse_state = 2;
            s->fd0a |= 1U;
            s->fd08 &= 0xBFFFU;
            s->pulse_count--;
        } else {
            s->pulse_state = 3;
            s->fd0a &= 0xFFFEU;
            s->fd08 |= 0x4000U;
            s->pulse_count = 3;
        }
    } else {
        s->pulse_state = 4;
        s->fd0a |= 1U;
        s->fd08 &= 0xBFFFU;
        s->pulse_count = 0;
    }
    if (s->published & 16U)
        s->fd0a |= 4U;
    else
        s->fd0a &= 0xFFFBU;
    if (s->published & 64U)
        s->fd08 |= 0x8000U;
    else
        s->fd08 &= 0x7FFFU;
}
void oem_digital_aux(OemDigital *s) {
    /* Divider10 entry2 at2BA18. It does not run at every base publication. */
    if (s->published & 8U)
        s->fd0a &= 0xFFEFU;
    else
        s->fd0a |= 16U;
}
