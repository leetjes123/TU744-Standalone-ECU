#include "control.h"
#include "lifecycle.h"
void fuel_plan(u32 now, const Rotation *r, const u8 *c, EnginePlan *p) {
    Controls *s = &ecu.control;
    Sensors *in = &ecu.sensors;
    u16 load, axis, dead, duration;
    u32 pw, period, limit, age;
    s32 kelvin;
    AxisAt at_rpm, at_load, at_clt;
    load = (c[0x5D4] & CFG_ALPHA_N) ? (u16)(in->tps.value / 10) : (u16)in->map.value;
    axis = (c[0x5D4] & CFG_ALPHA_N) ? 0x440 : 0x420;
    /* Each axis is located once per pass (identical results to table1/2). */
    axis_at(c + 0x400, 16, (s16)r->rpm, 0, &at_rpm);
    axis_at(c + axis, 16, (s16)load, 0, &at_load);
    axis_at(c + 0x460, 16, in->clt.value, 1, &at_clt);
    s->cell_rpm = at_rpm.index;
    s->cell_rpm_fraction = at_rpm.fraction;
    s->cell_load = at_load.index;
    s->cell_load_fraction = at_load.fraction;
    s->ve = table2_at(c, 0, &at_rpm, &at_load);
    if (s->mode == ENGINE_CRANKING)
        s->ve = table1_at(c, 0x490, &at_clt, 1);
    else if (vehicle.antilag)
        s->ve = c[0x7B9];
    s->target_afr = table2_at(c, 0x200, &at_rpm, &at_load);
    s->warmup = (u8)table1_at(c, 0x480, &at_clt, 0);
    duration = (u16)(table1_at(c, 0x4C0, &at_clt, 0) * 100U);
    age = now - s->running_at;
    s->afterstart =
        s->mode == ENGINE_RUNNING && age < duration
            ? (u8)(100U + (u32)(table1_at(c, 0x4B0, &at_clt, 0) - 100U) *
                             (duration - age) / duration)
            : 100;
    /* Current AE and complete cut policy are available before lambda gating. */
    lambda_update(now, c, p);
    /* Cranking VE replaces the running VE table; both use the air-charge
       calculation. Running enrichments are not applied twice at start. */
    pw = get16(c + 0x5DF);
    kelvin = (s32)in->iat.value * 100L + 27315L;
    pw = scale32(pw, 27315, (u16)kelvin);
    pw = scale32(pw, s->ve, 100);
    if (!(c[0x5D4] & CFG_ALPHA_N))
        pw = scale32(pw, (u16)in->map.value, 100);
    if (c[0x8D0])
        pw = scale32(pw, get16(c + CAL_STOICH), s->target_afr);
    pw /= 2UL; /* required fuel per 720 deg; two paired events per cycle */
    if (s->mode != ENGINE_CRANKING) {
        pw = scale32(pw, s->warmup, 100);
        pw = scale32(pw, s->afterstart, 100);
        pw = scale32(pw, s->applied_trim, 1024);
        pw = scale32(pw, s->ae_percent, 100);
    }
    /* Bound before adding dead time, including saturated scale32 results. */
    if (pw > 60000UL)
        pw = 60000UL;
    dead = voltage_table(c, 0x540, (u16)in->battery.value);
    if (pw)
        pw += dead;
    p->requested_us = (u16)(pw > 65535UL ? 65535UL : pw);
    period = r->rpm ? 60000000UL / r->rpm : 0;
    limit = period * c[0x8CF] / 100UL;
    if (limit > 25000UL)
        limit = 25000UL;
    if (pw > limit)
        pw = limit;
    p->pulse_us = (u16)pw;
    p->dwell_us = voltage_table(c, 0x530, (u16)in->battery.value);
    p->dwell_feedback = c[CAL_DWELL_FEEDBACK];
    if (period <= (u32)p->dwell_us + 1000UL)
        p->dwell_us = period > 1500UL ? (u16)(period - 1000UL) : 0;
    p->advance10 = (s16)((s16)table2_at(c, 0x100, &at_rpm, &at_load) * 5 - 200);
    p->advance10 = (s16)clamp32((s32)p->advance10 - (s32)table1(c, 0x510, in->iat.value, 0) * 5L +
                                    s->idle_spark10,
                                -200, 500);
    if (s->mode == ENGINE_CRANKING)
        p->advance10 = (s16)get16(c + CAL_CRANK_ADV);
    else if (vehicle.antilag)
        p->advance10 = (s16)((s16)c[0x7B8] * 5 - 200);
    p->trigger10 = (s16)get16(c + 0x605);
    p->injection_phase10 = get16(c + CAL_INJ_PHASE);
}
