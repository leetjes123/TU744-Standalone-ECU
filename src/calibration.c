#include "ecu.h"
#include "lifecycle.h"
#include "faults.h"
#include "diagnostic_monitors.h"
#include "knock.h"
#include <string.h>
const u8 *cal_active(void) {
    return ecu.cal.bytes[ecu.cal.active];
}
void cal_init(void) {
    memset(&ecu.cal, 0, sizeof(ecu.cal));
}
static u8 fail(u16 *error, u16 at) {
    *error = at;
    return 0;
}
u8 cal_validate(const u8 *c, u16 *error) {
    u16 i, a, b, base;
    s16 sa, sb;
    if (c[CAL_MAGIC] != 'L' || c[CAL_MAGIC + 1] != 'R' || get16(c + CAL_MAGIC + 2) != CAL_SCHEMA)
        return fail(error, CAL_MAGIC);
    for (base = 0x400; base <= 0x460; base += 0x20) {
        for (i = 0; i < 16; i++) {
            a = get16(c + base + 2 * i);
            if (base == 0x460) {
                sa = (s16)a;
                if (sa < -40 || sa > 150)
                    return fail(error, (u16)(base + 2 * i));
                if (i && sa <= (s16)get16(c + base + 2 * i - 2))
                    return fail(error, base);
            } else {
                b = base == 0x400 ? 12000U : (base == 0x420 ? 600U : 100U);
                if (a > b || (i && a <= get16(c + base + 2 * i - 2)))
                    return fail(error, base);
            }
        }
    }
    for (i = 0; i < 256; i++) {
        if (c[0x100 + i] > 140)
            return fail(error, (u16)(0x100 + i)); /* -20..50 deg */
        if (c[0x200 + i] < 70 || c[0x200 + i] > 220)
            return fail(error, (u16)(0x200 + i));
        if (c[0x300 + i] > 100)
            return fail(error, (u16)(0x300 + i));
    }
    for (i = 0; i < 16; i++) {
        if (get16(c + 0x490 + 2 * i) > 1000 || c[0x4B0 + i] < 100 ||
            get16(c + 0x4D0 + 2 * i) < 500 ||
            get16(c + 0x4D0 + 2 * i) > 2500)
            return fail(error, 0x490);
        if (c[0x4F0 + i] > get16(c + CAL_IAC_MAX) || c[0x500 + i] > get16(c + CAL_IAC_MAX) ||
            c[0x510 + i] > 60 || c[0x5F0 + i] > 100)
            return fail(error, 0x4F0);
        if (get16(c + 0x594 + 2 * i) > 600 ||
            (i && get16(c + 0x594 + 2 * i) < get16(c + 0x592 + 2 * i)))
            return fail(error, 0x594);
    }
    for (i = 0; i < 8; i++) {
        a = get16(c + 0x530 + 2 * i);
        b = get16(c + 0x540 + 2 * i);
        if (a < 500 || a > 6000 || b > 5000)
            return fail(error, 0x530);
        sa = (s16)get16(c + 0x610 + 2 * i);
        if (sa < -3000 || sa > 3000 || (i && sa <= (s16)get16(c + 0x60E + 2 * i)))
            return fail(error, 0x610);
        if (c[0x620 + i] > 80)
            return fail(error, 0x620);
        if (get16(c + 0x740 + 2 * i) > 10000 ||
            (i && get16(c + 0x740 + 2 * i) <= get16(c + 0x73E + 2 * i)))
            return fail(error, 0x740);
        if (get16(c + 0x789 + 2 * i) > 12000 ||
            (i && get16(c + 0x789 + 2 * i) <= get16(c + 0x787 + 2 * i)))
            return fail(error, 0x789);
    }
    for (base = 0x550; base <= 0x572; base += 0x22) {
        if (!get16(c + base) || get16(c + base) > 20000)
            return fail(error, base);
        for (i = 0; i < 16; i++) {
            a = get16(c + base + 2 + 2 * i);
            if (!a || (i && a >= get16(c + base + 2 * i)))
                return fail(error, base);
        }
    }
    for (i = 0; i < 6; i++) {
        if (c[0x628 + i] > 240)
            return fail(error, 0x628);
        if (c[0x750 + i] > 100 || (i && c[0x750 + i] <= c[0x74F + i]))
            return fail(error, 0x750);
    }
    for (i = 0; i < 48; i++)
        if (c[0x759 + i] < 100)
            return fail(error, (u16)(0x759 + i));
    if (c[0x5D4] & 0xC8U || c[0x5D8] > 2 || c[0x5E4] != 0 || c[0x600] > 1)
        return fail(error, 0x5D4);
    if ((s8)c[0x5E2] < 70 || (s8)c[0x5E2] > 120 || (s8)c[0x5E3] >= (s8)c[0x5E2])
        return fail(error, 0x5E2);
    a = get16(c + 0x5DB);
    b = get16(c + 0x5DD);
    if (a < 1500 || a > 10000 || b < 1000 || b >= a || get16(c + 0x5DF) > 20000 ||
        !get16(c + 0x5DF))
        return fail(error, 0x5DB);
    if (get16(c + 0x7B0) >= get16(c + 0x7B2) || get16(c + 0x7B2) > 1023 ||
        get16(c + 0x7B2) - get16(c + 0x7B0) < 100)
        return fail(error, 0x7B0);
    if (c[0x8CF] < 10 || c[0x8CF] > 95 || c[0x8D0] > 1 || c[0x5E6] > 20 || c[0x5EC] > 100)
        return fail(error, 0x8CF);
    if (c[0x731] < 128 || c[0x731] > 192 || c[0x732] > 128 || c[0x732] < 64 || !c[0x730])
        return fail(error, 0x730);
    if (c[0x8C3] >= c[0x8C4] || c[0x8C4] > 200 || !c[0x8C2] || c[0x8C2] > 16 || c[0x8C8] > c[0x8C9])
        return fail(error, 0x8C3);
    if (c[0x601] < 50 || c[0x602] > 250 || c[0x601] >= c[0x602])
        return fail(error, 0x601);
    if (get16(c + 0x630) > 10000 || get16(c + 0x632) > 10000 || get16(c + 0x634) != 0)
        return fail(error, 0x630); /* PI: no unqualified D term */
    if (get16(c + CAL_CRANK_RPM) < 100 || get16(c + CAL_RUN_RPM) <= get16(c + CAL_CRANK_RPM) ||
        get16(c + CAL_RUN_RPM) > 2000)
        return fail(error, CAL_RUN_RPM);
    if (get16(c + CAL_RUN_MS) < 100 || get16(c + CAL_RUN_MS) > 5000)
        return fail(error, CAL_RUN_MS);
    if (get16(c + CAL_AE_DECAY) < 10 || get16(c + CAL_AE_DECAY) > 5000 ||
        get16(c + CAL_STFT_KI) > 2000)
        return fail(error, CAL_AE_DECAY);
    if (get16(c + CAL_MAX_MAP) < 100 || get16(c + CAL_MAX_MAP) > 600 || !get16(c + CAL_IAC_MAX) ||
        get16(c + CAL_IAC_MAX) > 220)
        return fail(error, CAL_MAX_MAP);
    if (get16(c + CAL_STOICH) < 70 || get16(c + CAL_STOICH) > 220 || !get16(c + CAL_VSS_PPM) ||
        get16(c + CAL_VSS_PPM) > 100)
        return fail(error, CAL_STOICH);
    /* Both paired events must land on present teeth: n and n+30, n <= 27. */
    if (c[CAL_FLAGS] & 0xFCU || get16(c + 0x605) > 3599 || get16(c + CAL_INJ_PHASE) > 1620 ||
        get16(c + CAL_INJ_PHASE) % 60U)
        return fail(error, CAL_INJ_PHASE);
    if (c[CAL_DWELL_FEEDBACK] > 1U)
        return fail(error, CAL_DWELL_FEEDBACK);
    if ((s16)get16(c + CAL_CRANK_ADV) < -100 || (s16)get16(c + CAL_CRANK_ADV) > 200 ||
        get16(c + CAL_HOME_MS) < 3000 || get16(c + CAL_HOME_MS) > 10000)
        return fail(error, CAL_HOME_MS);
    if (get16(c + CAL_MIN_BAT) < 5000 || get16(c + CAL_MIN_BAT) > 12000 ||
        get16(c + CAL_SENSOR_AGE) < 20 || get16(c + CAL_SENSOR_AGE) > 100 ||
        get16(c + CAL_PLAN_AGE) < 20 || get16(c + CAL_PLAN_AGE) > 50)
        return fail(error, CAL_MIN_BAT);
    if (c[0x7B7] > 100 || get16(c + 0x7B5) >= get16(c + 0x5DB) ||
        ((c[0x7B4] & 0x80U) && !(c[0x7B4] & 0x40U)))
        return fail(error, 0x7B4);
    if ((c[0x7B4] & 0x40U) &&
        (!(c[0x5D4] & CFG_LAUNCH) || !get16(c + CAL_LAUNCH_MS) ||
         get16(c + CAL_ANTILAG_MS) < 100U || get16(c + CAL_ANTILAG_MS) > 3000U ||
         c[CAL_ANTILAG_CLT] < 60U || c[CAL_ANTILAG_CLT] > 110U ||
         c[CAL_ANTILAG_IAT] > 80U || c[0x7B8] > 140U || !c[0x7B9]))
        return fail(error, CAL_ANTILAG_MS);
    if (get16(c + 0x7BA) > get16(c + 0x7BC) || get16(c + 0x7BC) > 12000 || c[0x7BF] > 100 ||
        c[0x8C5] > 100)
        return fail(error, 0x7BA);
    if ((c[0x5D4] & CFG_LAUNCH) &&
        (!c[0x7AF] || get16(c + 0x5E7) < 1500 || get16(c + 0x5E7) > get16(c + 0x5DB)))
        return fail(error, 0x5E7);
    if (get16(c + 0x5E9) > 10000 || !c[0x758] || c[0x758] > 50 || c[0x757])
        return fail(error, 0x5E9);
    if (get16(c + CAL_DFCO_EXIT_RPM) > 2000U)
        return fail(error, CAL_DFCO_EXIT_RPM);
    if (c[0x5D9] != 5 || c[0x5DA] != 6)
        return fail(error, 0x5D9); /* fixed development IAC waveform */
    if (get16(c + CAL_IDLE_STEP_MS) < 20 || get16(c + CAL_IDLE_STEP_MS) > 1000)
        return fail(error, CAL_IDLE_STEP_MS);
    /* Homing must cover the full travel and finish inside its timeout. */
    a = get16(c + CAL_IAC_HOME_STEPS);
    if (a && (a < get16(c + CAL_IAC_MAX) || a > 800U))
        return fail(error, CAL_IAC_HOME_STEPS);
    if ((u32)(a ? a : IAC_HOME_STEPS_DEFAULT) * IAC_HOME_MS_PER_STEP > get16(c + CAL_HOME_MS))
        return fail(error, CAL_IAC_HOME_STEPS);
    if (c[CAL_WB_POLICY] > 1U || c[CAL_WB_POLICY + 1U])
        return fail(error, CAL_WB_POLICY);
    if (c[0x600] && !(c[CAL_FLAGS] & EQUIP_UPSTREAM_RELAY_HEATER))
        return fail(error, CAL_FLAGS);
    if (c[CAL_WB_POLICY] &&
        (get16(c + CAL_WB_WARM_MS) < 10000U || get16(c + CAL_WB_WARM_MS) > 60000U ||
         get16(c + CAL_WB_GOOD_MS) < 500U || get16(c + CAL_WB_GOOD_MS) > 10000U ||
         get16(c + CAL_WB_MIN_MV) < 10U || get16(c + CAL_WB_MAX_MV) > 4990U ||
         get16(c + CAL_WB_MIN_MV) >= get16(c + CAL_WB_MAX_MV)))
        return fail(error, CAL_WB_WARM_MS);
    if (get16(c + CAL_LAUNCH_MS) > 30000U ||
        (get16(c + CAL_LAUNCH_MS) && get16(c + CAL_LAUNCH_MS) < 1000U) ||
        c[CAL_LAUNCH_SOFT_PERCENT] > 100U ||
        (get16(c + CAL_LAUNCH_SOFT_RPM) &&
         (get16(c + CAL_LAUNCH_SOFT_RPM) < 1000U ||
          get16(c + CAL_LAUNCH_SOFT_RPM) >= get16(c + 0x5E7))))
        return fail(error, CAL_LAUNCH_MS);
     if ((c[CAL_DTC_ENABLE + 13U] & 0xF8U) ||
        c[CAL_DTC_FAIL_COUNT] > 100U || c[CAL_DTC_PASS_COUNT] > 100U ||
        (get16(c + CAL_DTC_TRIM_MS) &&
         (get16(c + CAL_DTC_TRIM_MS) < 1000U || get16(c + CAL_DTC_TRIM_MS) > 60000U)) ||
        (get16(c + CAL_DTC_O2_ACTIVITY_MS) &&
         (get16(c + CAL_DTC_O2_ACTIVITY_MS) < 1000U ||
          get16(c + CAL_DTC_O2_ACTIVITY_MS) > 30000U)) ||
        (get16(c + CAL_DTC_O2_SLOW_MS) &&
         (get16(c + CAL_DTC_O2_SLOW_MS) < 100U || get16(c + CAL_DTC_O2_SLOW_MS) > 10000U)) ||
         c[CAL_DTC_MISFIRE_PERCENT] > 100U || c[CAL_DTC_MISFIRE_COUNT] > 100U ||
         get16(c + CAL_DTC_PHASE_TIMEOUT_MS) > 10000U || c[CAL_DTC_OUTPUT_FAIL_COUNT] > 100U ||
         c[CAL_DTC_PHASE_POLARITY] > 1U)
         return fail(error, CAL_DTC_ENABLE);
    for (i = 0; i < 107U; i++)
        if (c[CAL_DTC_SUBTYPE_ENABLE + i] & 0xF0U)
            return fail(error, (u16)(CAL_DTC_SUBTYPE_ENABLE + i));
    if ((get16(c + CAL_DTC_PHASE_MIN_TICKS) || get16(c + CAL_DTC_PHASE_MAX_TICKS) ||
         get16(c + CAL_DTC_PHASE_DELTA_TICKS)) &&
        (get16(c + CAL_DTC_PHASE_MIN_TICKS) >= get16(c + CAL_DTC_PHASE_MAX_TICKS) ||
         !get16(c + CAL_DTC_PHASE_DELTA_TICKS) ||
         get16(c + CAL_DTC_PHASE_DELTA_TICKS) >= get16(c + CAL_DTC_PHASE_MAX_TICKS)))
        return fail(error, CAL_DTC_PHASE_MIN_TICKS);
    for (i = 0; i < 5; i++)
        if (get16(c + 0x7A3 + 2 * i) < 10 || get16(c + 0x7A3 + 2 * i) > 1000)
            return fail(error, 0x7A3);
    if (get16(c + 0x7A1) < 10 || get16(c + 0x7A1) > 1000 || get16(c + 0x7AD) < 500 ||
        get16(c + 0x7AD) > 4000)
        return fail(error, 0x7A1);
    if (!knock_validate(c, error)) return 0;
    sb = 0;
    *error = (u16)sb;
    return 1;
}
static void cal_touch(void) {
    u16 lock = hal_lock();
    ecu.cal.touched_at = ecu.milliseconds;
    hal_unlock(lock);
}
u8 cal_begin(void) {
    if (ecu.cal.staging || ecu.storage.phase)
        return 0;
    memcpy(ecu.cal.bytes[ecu.cal.active ^ 1U], cal_active(), CAL_SIZE);
    ecu.cal.staging = 1;
    cal_touch();
    return 1;
}
u8 cal_write(u16 at, const u8 *p, u8 n) {
    if (!ecu.cal.staging || !n || n > 32 || at > CAL_SIZE || n > CAL_SIZE - at)
        return 0;
    memcpy(ecu.cal.bytes[ecu.cal.active ^ 1U] + at, p, n);
    cal_touch();
    return 1;
}
/* Ordered, half-open ranges: preserve the first forbidden offset without
   calling a range predicate for every byte during a running tuning write. */
static const u16 structural_ranges[][2] = {
    {0x460, 0x480}, {0x550, 0x5B4}, {0x5D4, 0x5D5}, {0x5E4, 0x5E5},
    {0x600, 0x603}, {0x605, 0x607}, {0x7A1, 0x7AF}, {0x7B0, 0x7B4},
    {0x900, CAL_DFCO_EXIT_RPM}, {CAL_DFCO_EXIT_RPM + 2U, CAL_SIZE}};
/* A commit is two steps so a running tuning write can place each between
   control releases (each is several ms; together they exceeded the plan age).
   Only the foreground protocol writes staging, and it waits between steps. */
u8 cal_commit_check(void) {
    if (!ecu.cal.staging)
        return 0;
    if (!cal_validate(ecu.cal.bytes[ecu.cal.active ^ 1U], &ecu.cal.error_offset)) {
        fault_set(FAULT_CAL, 2, ecu.milliseconds);
        return 0;
    }
    return 1;
}
u8 cal_commit(u8 stopped) {
    return (u8)(cal_commit_check() && cal_commit_apply(stopped));
}
u8 cal_commit_apply(u8 stopped) {
    u16 i, r, lock;
    u8 changed = 0;
    const u8 *old = cal_active();
    const u8 *next = ecu.cal.bytes[ecu.cal.active ^ 1U];
    if (!ecu.cal.staging)
        return 0;
    for (r = 0; r < sizeof(structural_ranges) / sizeof(structural_ranges[0]); r++) {
        for (i = structural_ranges[r][0]; i < structural_ranges[r][1]; i++)
            if (next[i] != old[i]) {
                changed = 1;
                if (!stopped) {
                    ecu.cal.error_offset = i;
                    return 0;
                }
            }
    }
    /* Only foreground consumes calibration. ISRs consume a copied EnginePlan.
       The old bank cannot be a live ISR reader when it is reused. */
    lock = hal_lock();
    if (changed && (ecu.iac.state == IAC_HOMING || ecu.control.mode != ENGINE_STOPPED ||
                    (ecu.rotation.seen && ecu.rotation.state != ROT_UNSYNCED))) {
        hal_unlock(lock);
        return 0;
    }
    ecu.cal.active ^= 1U;
    ecu.cal.generation++;
    ecu.cal.valid = 1;
    ecu.cal.staging = 0;
    ecu.cal.dirty = 1;
    hal_unlock(lock);
    fault_set(FAULT_CAL, 0, ecu.milliseconds);
    if (changed) {
        memset(&ecu.sensors, 0, sizeof(ecu.sensors));
        ecu.control.ae_seeded = 0;
        ecu.control.ae_peak = 100;
        ecu.control.ae_percent = 100;
        ecu.control.afterstart = 100;
        ecu.control.trim_q16 = 65536L;
        ecu.control.applied_trim = 1024;
        ecu.control.trim_enabled = 0;
        ecu.control.previous_key = 0;
        iac_disable();
    }
    return 1;
}
/* Protocol 32: whole cells of the four 16x16 maps written straight into the
   active bank, without a staging copy, so a tool can tune while running.
   cal_validate checks these maps only per cell (VE not at all), so a cell
   inside its range keeps the image valid. Only the foreground consumes
   calibration and this runs in the foreground. Persistence stays command 24. */
u8 cal_live_write(u16 at, const u8 *p, u8 n) {
    static const u8 low[4] = {0, 0, 70, 0}, high[4] = {255, 140, 220, 100};
    u8 map = (u8)(at >> 8), i;
    u16 lock;
    if (!ecu.cal.valid || ecu.cal.staging || ecu.storage.phase || !n || n > 16 ||
        at >= 0x400U || (at & 0xFFU) + n > 256U)
        return 0;
    for (i = 0; i < n; i++)
        if (p[i] < low[map] || p[i] > high[map])
            return 0;
    lock = hal_lock();
    memcpy(ecu.cal.bytes[ecu.cal.active] + at, p, n);
    ecu.cal.live_edits++;
    ecu.cal.dirty = 1;
    hal_unlock(lock);
    return 1;
}
void cal_abort(void) {
    ecu.cal.staging = 0;
}
void cal_poll(u32 now) {
    /* One foreground transport owns volatile staging. Expiry never touches
       the active bank or an admitted durable storage transaction. */
    if (ecu.cal.staging && now - ecu.cal.touched_at >= CAL_TRANSACTION_MS)
        cal_abort();
}
void cal_example(u8 *c) {
    u16 i;
    static const u8 dtc_events[] = {0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0A, 0x0B, 0x0C,
                                    0x0D, 0x1B, 0x27, 0x2D, 0x3F, 0x40, 0x43, 0x45,
                                    0x1A, 0x4B, 0x4A, 0x4D, 0x50, 0x56,
                                    0x5B, 0x61, 0x65, 0x68};
    static const u16 r[16] = {45000, 30000, 20000, 14000, 10000, 7000, 5000, 3500,
                              2500,  1800,  1300,  950,   700,   520,  390,  290};
    memset(c, 0, CAL_SIZE);
    for (i = 0; i < 256; i++) {
        c[i] = 60;
        c[0x100 + i] = 60;
        c[0x200 + i] = 147;
    }
    for (i = 0; i < 16; i++) {
        put16(c + 0x400 + 2 * i, (u16)(i * 500));
        put16(c + 0x420 + 2 * i, (u16)(20 + i * 20));
        put16(c + 0x440 + 2 * i, (u16)(i * 100 / 15));
        put16(c + 0x460 + 2 * i, (u16)(-30 + (s16)i * 10));
        c[0x480 + i] = 100;
        put16(c + 0x490 + 2 * i, 200);
        c[0x4B0 + i] = 110;
        c[0x4C0 + i] = 30;
        put16(c + 0x4D0 + 2 * i, 950);
        c[0x4F0 + i] = 30;
        c[0x500 + i] = 40;
        c[0x5F0 + i] = (u8)(i * 5);
        put16(c + 0x552 + 2 * i, r[i]);
        put16(c + 0x574 + 2 * i, r[i]);
        put16(c + 0x594 + 2 * i, (u16)(i * 20));
    }
    put16(c + 0x550, 2490);
    put16(c + 0x572, 2490);
    for (i = 0; i < 8; i++) {
        put16(c + 0x530 + 2 * i, 3000);
        put16(c + 0x540 + 2 * i, 850);
        put16(c + 0x610 + 2 * i, (u16)(-700 + (s16)i * 200));
        c[0x620 + i] = (u8)(26 + i * 4); /* Negative error must retard spark. */
        put16(c + 0x740 + 2 * i, (u16)(50 + i * 200));
        put16(c + 0x789 + 2 * i, (u16)(500 + i * 1000));
        c[0x799 + i] = 50;
    }
    for (i = 0; i < 6; i++) {
        c[0x750 + i] = (u8)(i * 20);
        c[0x628 + i] = 64;
    }
    for (i = 0; i < 48; i++)
        c[0x759 + i] = 100;
    c[0x5D8] = 1;
    c[0x5D9] = 5;
    c[0x5DA] = 6;
    put16(c + 0x5DB, 6500);
    put16(c + 0x5DD, 6200);
    put16(c + 0x5DF, 5000);
    c[0x5E2] = 95;
    c[0x5E3] = 90;
    c[0x5E5] = 2;
    c[0x5E6] = 2;
    c[0x5EC] = 20;
    c[0x5EE] = 50;
    put16(c + 0x5E7, 3500);
    put16(c + 0x5E9, 1800);
    /* 0x5EB is reserved; DFCO no longer depends on MAP. */
    c[0x603] = 1; /* 100 ms; preserve the existing 0.1 s tuning resolution. */
    c[0x604] = 30;
    c[0x601] = 100;
    c[0x602] = 200;
    put16(c + 0x630, 2000);
    put16(c + 0x632, 500);
    c[0x730] = 50;
    c[0x731] = 141;
    c[0x732] = 115;
    c[0x756] = 40;
    c[0x758] = 2;
    put16(c + 0x7A1, 394);
    for (i = 0; i < 5; i++)
        put16(c + 0x7A3 + 2 * i, (u16)(347 - i * 60));
    put16(c + 0x7AD, 1780);
    put16(c + 0x7B0, 50);
    put16(c + 0x7B2, 950);
    put16(c + 0x7BA, 600);
    put16(c + 0x7BC, 5500);
    c[0x7BE] = 60;
    c[0x8C0] = 4;
    c[0x8C2] = 1;
    c[0x8C3] = 80;
    c[0x8C4] = 100;
    c[0x8C5] = 78;
    c[0x8C7] = 10;
    c[0x8C8] = 145;
    c[0x8C9] = 149;
    c[0x8CF] = 85;
    c[0x8D0] = 1;
    c[CAL_MAGIC] = 'L';
    c[CAL_MAGIC + 1] = 'R';
    put16(c + CAL_MAGIC + 2, CAL_SCHEMA);
    put16(c + CAL_RUN_RPM, 650);
    put16(c + CAL_CRANK_RPM, 450);
    put16(c + CAL_RUN_MS, 300);
    put16(c + CAL_AE_DECAY, 500);
    put16(c + CAL_STFT_KI, 100);
    put16(c + CAL_MAX_MAP, 250);
    put16(c + CAL_IAC_MAX, 180);
    put16(c + CAL_STOICH, 147);
    put16(c + CAL_VSS_PPM, 5);
    put16(c + CAL_INJ_PHASE, 1620);
    put16(c + CAL_CRANK_ADV, 100);
    put16(c + CAL_HOME_MS, 4000);
    put16(c + CAL_MIN_BAT, 6000);
    put16(c + CAL_SENSOR_AGE, 50);
    put16(c + CAL_PLAN_AGE, 30);
    put16(c + CAL_IDLE_STEP_MS, 60);
    put16(c + CAL_DFCO_EXIT_RPM, DFCO_EXIT_RPM_DEFAULT);
    for (i = 0; i < sizeof(dtc_events); i++) {
        c[CAL_DTC_ENABLE + dtc_events[i] / 8U] |= (u8)(1U << (dtc_events[i] & 7U));
        c[CAL_DTC_SUBTYPE_ENABLE + dtc_events[i]] = 0x0FU;
    }
    c[CAL_DTC_FAIL_COUNT] = 3;
    c[CAL_DTC_PASS_COUNT] = 3;
    put16(c + CAL_DTC_TRIM_MS, 10000);
    put16(c + CAL_DTC_O2_ACTIVITY_MS, 5000);
    put16(c + CAL_DTC_O2_SLOW_MS, 1500);
    c[CAL_DTC_MISFIRE_PERCENT] = 35;
    c[CAL_DTC_MISFIRE_COUNT] = 4;
    put16(c + CAL_DTC_PHASE_TIMEOUT_MS, 2000);
    c[CAL_DTC_OUTPUT_FAIL_COUNT] = 5;
    c[CAL_DTC_VSS_MAX_KPH] = 250;
    put16(c + CAL_DTC_PHASE_MIN_TICKS, 66);
    put16(c + CAL_DTC_PHASE_MAX_TICKS, 168);
    put16(c + CAL_DTC_PHASE_DELTA_TICKS, 18);
    c[CAL_DTC_PHASE_POLARITY] = 0;
    put16(c + CAL_DTC_TPS_SLEW, 30000);
    put16(c + CAL_DTC_MAP_SLEW, 3000);
    put16(c + CAL_DTC_TEMP_SLEW, 100);
    put16(c + CAL_DTC_BATTERY_SLEW, 50000);
    knock_defaults(c);
}
