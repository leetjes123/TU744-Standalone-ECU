#include "control.h"
void idle_update(u32 now, u16 dt, const Rotation *r, const u8 *c) {
    Controls *s = &ecu.control;
    u16 base, start, duration, maximum, fan_steps;
    u32 age;
    u8 mode, ix;
    s32 error, p, change, output;
    u16 fraction;
    AxisAt at_clt;
    maximum = get16(c + CAL_IAC_MAX);
    axis_at(c + 0x460, 16, ecu.sensors.clt.value, 1, &at_clt);
    s->idle_target = table1_at(c, 0x4D0, &at_clt, 1);
    base = table1_at(c, 0x4F0, &at_clt, 0);
    start = table1_at(c, 0x500, &at_clt, 0);
    duration = (u16)c[0x5EE] * 100U;
    age = now - s->running_at;
    if (s->mode == ENGINE_CRANKING)
        mode = IDLE_CRANK;
    else if (s->mode != ENGINE_RUNNING || ecu.sensors.tps.quality != QUALITY_VALID ||
             ecu.sensors.clt.quality != QUALITY_VALID)
        mode = IDLE_UNAVAILABLE;
    else if (age < duration)
        mode = IDLE_CATCH;
    else if (ecu.sensors.tps.value > (s16)c[0x5E6] * 10)
        mode = IDLE_OFF;
    else if (s->dfco || r->rpm > s->idle_target + 500U)
        mode = IDLE_RETURN;
    else
        mode = IDLE_FEEDBACK;
    if (mode == IDLE_CRANK)
        base = (u16)(base + start);
    else if (mode == IDLE_CATCH && duration)
        base = (u16)(base + (u32)start * (duration - age) / duration);
    fan_steps = s->fan_request ? c[0x8C0] : 0;
    base = (u16)(base + fan_steps);
    error = (s32)s->idle_target - r->rpm;
    if (error > -(s32)c[0x604] && error < (s32)c[0x604])
        error = 0;
    p = error * get16(c + 0x630) / 10000L;
    if (s->idle_mode != mode || s->previous_generation != ecu.cal.generation) {
        /* Track the realizable output on entry and on live gain/map changes. */
        s->idle_integral =
            clamp32(((s32)s->idle_position + fan_steps - s->idle_fan_steps - base - p) * 1000L,
                    -220000L, 220000L);
    }
    s->idle_spark10 = 0;
    if (mode == IDLE_FEEDBACK) {
        fraction = axis_fraction(c + 0x610, 8, (s16)error, 1, &ix);
        s->idle_spark10 = (s16)((s32)lerp(c[0x620 + ix], c[0x621 + ix], fraction) * 5L - 200L);
        if (c[0x5D8] != 2 && ecu.iac.state == IAC_READY) {
            change = error * get16(c + 0x632) / 100L * (s32)dt / 1000L;
            if (c[0x5D8] == 0) {
                change = 0;
                if (now - s->idle_step_at >= get16(c + CAL_IDLE_STEP_MS)) {
                    change = error > 0 ? 1000L : (error < 0 ? -1000L : 0L);
                    s->idle_step_at = now;
                }
            }
            output = (s32)base + p + s->idle_integral / 1000L;
            if (s->fan_request && !s->fan)
                change = 0; /* Do not integrate away the pending load preload. */
            if ((change > 0 && output < maximum) || (change < 0 && output > 0))
                s->idle_integral = clamp32(s->idle_integral + change, -220000L, 220000L);
            base = (u16)clamp32((s32)base + p + s->idle_integral / 1000L, 0, maximum);
        }
    } else if (mode == IDLE_RETURN && s->idle_position > base) {
        /* Bounded dashpot closure. */
        base = (u16)(s->idle_position - 1U);
    }
    if (s->fan_waiting && s->fan_request && base < s->fan_target)
        base = s->fan_target;
    s->idle_position = (u16)clamp32(base, 0, maximum);
    s->idle_fan_steps = fan_steps;
    s->idle_mode = mode;
    s->previous_generation = ecu.cal.generation;
    ecu.iac.target = s->idle_position;
}
