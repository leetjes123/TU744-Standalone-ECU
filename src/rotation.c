#include "ecu.h"
#include "diagnostic_monitors.h"
void rotation_snapshot(Rotation *out) {
    u16 lock = hal_lock();
    *out = ecu.rotation;
    hal_unlock(lock);
}
u8 rotation_edge(u32 stamp) SHARED {
    u32 dt, normal;
    u16 short_dt, short_normal, scaled_dt, upper;
    u8 gap, irregular;
    if (!ecu.rotation.seen) {
        ecu.rotation.seen = 1;
        ecu.rotation.last = stamp;
        return 0;
    }
    dt = stamp - ecu.rotation.last;
    normal = ecu.rotation.normal;
    if (dt < 100UL || dt < normal / 2UL)
        return 0;
    ecu.rotation.last = stamp;
    /* All scaled products fit in a C166 word in this domain. Keep the
       original widened comparisons for slow rotation and large intervals. */
    if (dt <= 7281UL && normal <= 7281UL) {
        short_dt = (u16)dt;
        short_normal = (u16)normal;
        scaled_dt = (u16)((short_dt << 2) + short_dt);
        upper = (u16)((short_normal << 3) + short_normal);
        irregular = (u8)(short_normal &&
            (scaled_dt < (short_normal << 1) + short_normal || scaled_dt > upper));
        /* A regular interval (0.6..1.8 normal) cannot be a gap (2.5..4.5).
           Reuse that classification without changing either strict bound. */
        gap = (u8)(irregular && (short_dt << 1) > (short_normal << 2) + short_normal &&
                   (short_dt << 1) < upper);
    } else {
        gap = (u8)(normal && dt * 2UL > normal * 5UL && dt * 2UL < normal * 9UL);
        irregular = (u8)(normal && (dt * 5UL < normal * 3UL || dt * 5UL > normal * 9UL));
    }
    if (dt > 250000UL) { /* 200 ms accepted-edge timeout, 60-2 only */
        ecu.rotation.state = ROT_UNSYNCED;
        ecu.rotation.normal = 0;
        ecu.rotation.have_gap = 0;
        ecu.rotation.rpm = 0;
        ecu.rotation.epoch++;
        safety_inhibit(INH_SYNC, ecu.milliseconds);
        diagnostic_rotation_edge(stamp, ecu.rotation.epoch, ecu.rotation.tooth, 0, 0);
        return 0;
    }
    if (gap) {
        ecu.rotation.cycle++;
        if (ecu.rotation.have_gap && ecu.rotation.tooth == 57) {
            ecu.rotation.revolution = stamp - ecu.rotation.gap_stamp;
            ecu.rotation.rpm = (u16)(75000000UL / ecu.rotation.revolution);
            ecu.rotation.state = ROT_VALID;
        } else {
            if (ecu.rotation.state == ROT_VALID)
                ecu.rotation.losses++;
            ecu.rotation.epoch++;
            ecu.rotation.state = ROT_ACQUIRING;
            ecu.rotation.rpm = 0;
            safety_inhibit(INH_SYNC, ecu.milliseconds);
        }
        ecu.rotation.gap_stamp = stamp;
        ecu.rotation.have_gap = 1;
        ecu.rotation.tooth = 0;
    } else {
        if (ecu.rotation.state != ROT_UNSYNCED &&
            (ecu.rotation.tooth >= 57 || irregular)) {
            ecu.rotation.losses++;
            ecu.rotation.epoch++;
            ecu.rotation.state = ROT_UNSYNCED;
            ecu.rotation.have_gap = 0;
            ecu.rotation.rpm = 0;
            safety_inhibit(INH_SYNC, ecu.milliseconds);
        }
        if (ecu.rotation.tooth < 255)
            ecu.rotation.tooth++;
        ecu.rotation.normal = dt;
    }
    diagnostic_rotation_edge(stamp, ecu.rotation.epoch, ecu.rotation.tooth, gap,
                             (u8)(ecu.rotation.state == ROT_VALID));
    return (u8)(ecu.rotation.state == ROT_VALID);
}
/* Leading run of regular synchronized teeth from cap[i]: each 16-bit capture
   difference is 100..7281 ticks and 0.6..1.8 of the previous one, the tooth
   stays below 56 and never reaches the tooth-30 misfire boundary. Only 16-bit
   register arithmetic; the caller applies the result once. */
typedef struct {
    u32 span;
    u16 normal, fastest;
    u8 teeth;
} ToothRun;
static void regular_run(volatile u16 SYSTEM_RAM *cap, u8 i, u8 count, u16 normal, u8 tooth,
                        ToothRun *run) {
    u16 previous = cap[i - 1U], current, dt, scaled, fastest = 0xFFFFU, span_low = 0, span_high = 0;
    u8 teeth = 0;
    while (i < count && tooth < 56U && tooth != 29U) {
        current = cap[i];
        dt = (u16)(current - previous);
        if (dt < 100U || dt > 7281U)
            break;
        scaled = (u16)((dt << 2) + dt);
        if (scaled < (u16)((normal << 1) + normal) || scaled > (u16)((normal << 3) + normal))
            break;
        if (dt < fastest)
            fastest = dt;
        span_low += dt;
        if (span_low < dt)
            span_high++;
        normal = dt;
        previous = current;
        tooth++;
        teeth++;
        i++;
    }
    run->span = ((u32)span_high << 16) | span_low;
    run->normal = normal;
    run->fastest = fastest;
    run->teeth = teeth;
}
static u32 capture_span(volatile u16 SYSTEM_RAM *cap, u8 count) {
    u16 previous = cap[0], current, low = 0, high = 0, dt;
    u8 i;
    for (i = 1; i < count; i++) {
        current = cap[i];
        dt = (u16)(current - previous);
        low += dt;
        if (low < dt)
            high++;
        previous = current;
    }
    return ((u32)high << 16) | low;
}
/* Decode one PEC capture block, as the OEM consumes half-turn blocks rather
   than individual edges. cap[] holds the low T1 words of `count` consecutive
   captures; the last one is `last_stamp`. Runs of regular synchronized teeth
   are checked by regular_run and the rotation state is written back once.
   Every other edge goes through rotation_edge, followed by the rule that a
   rejected edge revokes synchronization. Effects equal decoding each edge in
   order with rotation_edge; misfire-window eligibility is evaluated once for
   the block. *fastest receives the smallest accepted interval >= 100 ticks. */
u8 rotation_block(volatile u16 SYSTEM_RAM *cap, u8 count, u32 last_stamp, u8 eligible, u32 *fastest) SHARED {
    u32 stamp, interval, best = 0xFFFFFFFFUL, last = 0;
    u16 dt, normal = 0, scaled;
    u8 i, tooth = 0, cached = 0, accepted = 0, valid;
    ToothRun run;
    stamp = last_stamp - capture_span(cap, count);
    i = 0;
    for (;;) {
        if (!cached && ecu.rotation.state == ROT_VALID && ecu.rotation.seen &&
            ecu.rotation.normal && ecu.rotation.normal <= 7281UL) {
            last = ecu.rotation.last;
            normal = (u16)ecu.rotation.normal;
            tooth = ecu.rotation.tooth;
            cached = 1;
        }
        valid = 0;
        if (cached) {
            interval = stamp - last;
            if (tooth < 56U && tooth != 29U && interval >= 100UL && interval <= 7281UL) {
                dt = (u16)interval;
                scaled = (u16)((dt << 2) + dt);
                if (scaled >= (u16)((normal << 1) + normal) && scaled <= (u16)((normal << 3) + normal)) {
                    if (interval < best) best = interval;
                    last = stamp;
                    tooth++;
                    normal = dt;
                    accepted = 1;
                    if (!eligible)
                        diagnostic_rotation_ineligible();
                    valid = 1;
                }
            }
            if (!valid) {
                ecu.rotation.last = last;
                ecu.rotation.tooth = tooth;
                ecu.rotation.normal = normal;
                cached = 0;
            }
        }
        if (!valid) {
            interval = stamp - ecu.rotation.last;
            if (interval >= 100UL && interval < best) best = interval;
            valid = rotation_edge(stamp);
            accepted |= valid;
            /* T0 counts electrical edges. Rejected noise invalidates its angle
               relation; do not silently continue with an extra counter tooth. */
            if (!valid && ecu.rotation.state == ROT_VALID) {
                ecu.rotation.state = ROT_UNSYNCED;
                ecu.rotation.have_gap = 0;
                ecu.rotation.epoch++;
                ecu.rotation.losses++;
                safety_inhibit(INH_SYNC, ecu.milliseconds);
            }
        }
        if (++i >= count)
            break;
        if (cached) {
            regular_run(cap, i, count, normal, tooth, &run);
            if (run.teeth) {
                stamp += run.span;
                last = stamp;
                tooth = (u8)(tooth + run.teeth);
                normal = run.normal;
                if (run.fastest < best) best = run.fastest;
                accepted = 1;
                if (!eligible)
                    diagnostic_rotation_ineligible();
                i = (u8)(i + run.teeth);
                if (i >= count)
                    break;
            }
        }
        stamp += (u16)(cap[i] - cap[i - 1U]);
    }
    if (cached) {
        ecu.rotation.last = last;
        ecu.rotation.tooth = tooth;
        ecu.rotation.normal = normal;
    }
    *fastest = best;
    return accepted;
}
