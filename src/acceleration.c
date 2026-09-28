#include "control.h"
static u16 amount(const u8 *c, u16 rpm, u16 rate, u16 tps) {
    u8 x, y, i;
    u16 fx, fy, fa;
    s16 a, b, m;
    fx = axis_fraction(c + 0x740, 8, (s16)rate, 0, &x);
    y = 0;
    while (y < 4 && tps > c[0x751 + y])
        y++;
    fy = (u16)clamp32(((s32)tps - c[0x750 + y]) * 256L / (c[0x751 + y] - c[0x750 + y]), 0, 256);
    a = lerp(c[0x759 + 8U * y + x], c[0x75A + 8U * y + x], fx);
    b = lerp(c[0x761 + 8U * y + x], c[0x762 + 8U * y + x], fx);
    fa = axis_fraction(c + 0x789, 8, (s16)rpm, 0, &i);
    m = lerp(c[0x799 + i], c[0x79A + i], fa);
    /* Schema 4: 100% is neutral. RPM scales only the enrichment above 100%,
       so zero RPM modifier disables AE without removing base fuel. */
    return (u16)(100U + (u32)(lerp(a, b, fy) - 100) * (u16)m / 100UL);
}
void acceleration_update(u32 now, u16 dt, u16 rpm, const u8 *c) {
    Controls *s = &ecu.control;
    u32 age;
    u16 request, tps = (u16)ecu.sensors.tps.value;
    if (ecu.sensors.tps.quality != QUALITY_VALID || s->mode != ENGINE_RUNNING || !dt || dt > 50) {
        s->ae_seeded = 0;
        s->ae_percent = s->ae_peak = 100;
        s->ae_qualifying = 0;
        return;
    }
    if (!s->ae_seeded) {
        s->previous_tps = tps;
        s->ae_seeded = 1;
        s->tps_rate = 0;
        return;
    }
    s->tps_rate = (s16)clamp32(((s32)tps - s->previous_tps) * 100L / dt, -10000, 10000);
    if (s->tps_rate > c[0x756]) {
        if (!s->ae_qualifying) {
            s->ae_qualifying = 1;
            s->ae_qualify_at = now;
        }
        if (now - s->ae_qualify_at >= (u32)(c[0x758] - 1U) * 10UL) {
            request = amount(c, rpm, (u16)s->tps_rate, (u16)(s->previous_tps / 10U));
            if (request > s->ae_percent) {
                s->ae_peak = request;
                s->ae_at = now;
            }
        }
    } else
        s->ae_qualifying = 0;
    age = now - s->ae_at;
    s->ae_percent =
        age >= get16(c + CAL_AE_DECAY)
            ? 100
            : (u16)(100U + (u32)(s->ae_peak - 100U) *
                          (get16(c + CAL_AE_DECAY) - age) / get16(c + CAL_AE_DECAY));
    s->previous_tps = tps;
}
