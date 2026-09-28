#include "control.h"
#include "lifecycle.h"
void lambda_update(u32 now, const u8 *c, const EnginePlan *p) {
    Controls *s = &ecu.control;
    Sensors *in = &ecu.sensors;
    s32 delta, low, high, error, step;
    u32 elapsed;
    u8 eligible;
    low = (s32)c[0x732] * 512L;
    high = (s32)c[0x731] * 512L;
    if (s->lambda_type != c[0x600]) {
        s->trim_q16 = 65536L;
        s->lambda_type = c[0x600];
        s->last_trim = now;
    }
    s->trim_q16 = clamp32(s->trim_q16, low, high); /* clamp even during deadband/inhibit */
    eligible =
        (u8)((c[0x5D4] & CFG_STFT) && s->mode == ENGINE_RUNNING &&
             now - s->running_at >= (u32)c[0x8C7] * 1000UL && in->oxygen.quality == QUALITY_VALID &&
             in->clt.quality == QUALITY_VALID && in->tps.quality == QUALITY_VALID &&
             in->clt.value >= (s8)c[0x7BE] && p->rpm >= get16(c + 0x7BA) &&
             p->rpm <= get16(c + 0x7BC) && (!c[0x7BF] || in->tps.value <= (s16)c[0x7BF] * 10) &&
             !p->fuel_cut && !p->spark_cut && !p->soft_fuel && !p->soft_spark && s->ae_percent == 100U &&
             s->afterstart == 100U && !vehicle.antilag);
    if (c[0x600])
        eligible = (u8)(eligible && in->wideband_ready && in->afr10 > 0);
    else
        eligible = (u8)(eligible && s->target_afr >= c[0x8C8] && s->target_afr <= c[0x8C9]);
    if (!eligible) {
        /* Neutral application on every disable. History is reset for invalid
           feedback/start; short AE interruptions hold the bounded controller. */
        if (in->oxygen.quality != QUALITY_VALID || s->mode != ENGINE_RUNNING ||
            !in->wideband_ready && c[0x600])
            s->trim_q16 = 65536L;
        s->applied_trim = 1024;
        s->trim_enabled = 0;
        s->last_trim = now;
        return;
    }
    elapsed = now - s->last_trim;
    if (elapsed >= (u32)c[0x730] * 10UL) {
        if (elapsed > 2000UL)
            elapsed = 2000UL;
        if (c[0x600]) {
            error = (s32)in->afr10 - s->target_afr;
            if (error < 0 ? -error <= c[0x8C1] : error <= c[0x8C1])
                error = 0;
            /* Incremental integral of relative lambda error, gain per second.
               Scale before elapsed multiplication to remain within signed32. */
            delta = error * 65536L / (s->target_afr ? s->target_afr : 1);
            delta = delta * get16(c + CAL_STFT_KI) / 1000L;
            delta = delta * (s32)elapsed / 1000L;
            if (c[0x8CD]) {
                step = (s32)c[0x8CD] * 512L;
                delta = clamp32(delta, -step, step);
            }
        } else {
            if (in->oxygen_mv < (u16)c[0x8C3] * 5U)
                s->rich = 0;
            else if (in->oxygen_mv > (u16)c[0x8C4] * 5U)
                s->rich = 1;
            delta = (s32)c[0x8C2] * 512L;
            if (s->rich)
                delta = -delta;
        }
        s->trim_q16 = clamp32(s->trim_q16 + delta, low, high);
        s->last_trim = now;
    }
    s->applied_trim = (u16)(s->trim_q16 / 64L);
    s->trim_enabled = 1;
}
