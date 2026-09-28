#include "control.h"
#include "lifecycle.h"
void engine_state_update(u32 now, const Rotation *r, const u8 *c) {
    Controls *s = &ecu.control;
    if (!s->key_on || r->state != ROT_VALID) {
        s->mode = ENGINE_STOPPED;
        s->qualifying = 0;
        s->running_at = now;
        return;
    }
    if (s->mode == ENGINE_STOPPED)
        s->mode = ENGINE_CRANKING;
    if (s->mode == ENGINE_RUNNING && r->rpm < get16(c + CAL_CRANK_RPM)) {
        s->mode = ENGINE_CRANKING;
        s->qualifying = 0;
    }
    if (s->mode == ENGINE_CRANKING) {
        if (r->rpm >= get16(c + CAL_RUN_RPM)) {
            if (!s->qualifying) {
                s->qualifying = 1;
                s->qualify_at = now;
            }
            if (now - s->qualify_at >= get16(c + CAL_RUN_MS)) {
                s->mode = ENGINE_RUNNING;
                s->running_at = now;
                s->qualifying = 0;
            }
        } else
            s->qualifying = 0;
    }
}
void limits_update(u32 now, const Rotation *r, const u8 *c, EnginePlan *p) {
    Controls *s = &ecu.control;
    Sensors *in = &ecu.sensors;
    u16 threshold, exit_rpm;
    u8 method, eligible;
    p->fuel_cut = p->spark_cut = 0;
    p->soft_fuel = p->soft_spark = 0;
    if (r->rpm >= get16(c + 0x5DB))
        s->rev_limited = 1;
    else if (r->rpm <= get16(c + 0x5DD))
        s->rev_limited = 0;
    method = (u8)((c[0x7B4] >> 1) & 3U);
    if (s->rev_limited) {
        if (method != 2)
            p->fuel_cut |= CUT_REV;
        if (method != 1)
            p->spark_cut |= CUT_REV;
    }
    threshold = get16(c + 0x7B5);
    if ((c[0x7B4] & 1U) && threshold && r->rpm >= threshold) {
        u16 pct = (u16)((u32)(r->rpm - threshold) * c[0x7B7] / (get16(c + 0x5DB) - threshold));
        if (pct > 100)
            pct = 100;
        if (method != 2)
            p->soft_fuel = (u8)pct;
        if (method != 1)
            p->soft_spark = (u8)pct;
    }
    vehicle.main_soft_fuel = p->soft_fuel;
    vehicle.launch_soft = 0;
    s->launch = (u8)(launch_permitted(now) && in->tps.value >= (s16)c[0x8C5] * 10 &&
                     s->mode == ENGINE_RUNNING);
    if (s->launch && r->rpm >= get16(c + 0x5E7)) {
        method = (u8)((c[0x7B4] >> 4) & 3U);
        if (method != 2)
            p->fuel_cut |= CUT_LAUNCH;
        if (method != 1)
            p->spark_cut |= CUT_LAUNCH;
    }
    threshold = get16(c + CAL_LAUNCH_SOFT_RPM);
    if (s->launch && (c[0x7B4] & 8U) && threshold && r->rpm >= threshold) {
        u32 pct = (u32)(r->rpm - threshold) * c[CAL_LAUNCH_SOFT_PERCENT] /
                  (get16(c + 0x5E7) - threshold);
        method = (u8)((c[0x7B4] >> 4) & 3U);
        if (pct > c[CAL_LAUNCH_SOFT_PERCENT])
            pct = c[CAL_LAUNCH_SOFT_PERCENT];
        vehicle.launch_soft = (u8)pct;
        if (method != 2U && pct > p->soft_fuel)
            p->soft_fuel = (u8)pct;
        if (method != 1U && pct > p->soft_spark)
            p->soft_spark = (u8)pct;
    }
    exit_rpm = (u16)(s->idle_target + 200U);
    eligible =
        (u8)((c[0x5D4] & CFG_DFCO) && s->mode == ENGINE_RUNNING &&
             in->tps.quality == QUALITY_VALID && in->map.quality == QUALITY_VALID &&
             in->clt.quality == QUALITY_VALID && in->clt.value >= 60 && in->tps.value <= 20 &&
             in->map.value <= c[0x5EB] && r->rpm > (s->dfco ? exit_rpm : get16(c + 0x5E9)));
    if (!eligible) {
        s->dfco = 0;
        s->dfco_waiting = 0;
    } else {
        if (!s->dfco_waiting) {
            s->dfco_waiting = 1;
            s->dfco_at = now;
        }
        if (now - s->dfco_at >= (u32)c[0x603] * 100UL)
            s->dfco = 1;
    }
    if (s->dfco)
        p->fuel_cut |= CUT_DFCO;
    if (s->mode == ENGINE_CRANKING && in->tps.quality == QUALITY_VALID && in->tps.value >= 900)
        p->fuel_cut |= CUT_FLOOD;
    if (in->map.quality == QUALITY_VALID && in->map.value > get16(c + CAL_MAX_MAP)) {
        p->fuel_cut |= CUT_BOOST;
        p->spark_cut |= CUT_BOOST;
    }
    antilag_update(now, r, p);
}
