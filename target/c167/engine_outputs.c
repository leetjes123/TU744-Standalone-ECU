#include "board.h"
#include "lifecycle.h"
#include "oem_timing.h"
#include "oem_ignition.h"
#include "knock.h"
#include <string.h>
/* Module state lives in on-chip RAM (SDATA, zero-initialized): it is used by
   every compare interrupt, and external RAM is an 8-bit bus with wait states. */

/* ---- OEM segment ignition (sub_37CA0 port, src/oem_ignition.c) ----------
   The ROM schedules each coil once per 180-degree segment, straight after the
   boundary capture (teeth 1 and 31). Its three compares map onto per-coil
   channels here: CC0 (fire, coil F7FE) -> fire channel of that coil; CC6
   (charge, coil F7F6) and CC4 (second charge, coil F7F4) -> the charge-start
   channel of their coil. Coarse stages compare T0, fine stages use the ROM
   formulas (oem_angle_refine); a compare the ROM would service at once (IR
   set while the pass runs at low priority) is applied through the hook at
   that point. The timed fire edge stays a hardware toggle, installed only
   while the coil charges; charges still pass the shared safety owner. */
#define STAGE_NONE 0U
#define STAGE_COARSE 1U
#define STAGE_TIMED 2U
typedef struct {
    u16 value, correction, predicted, epoch;
    u8 mode, fraction, full, cc6;
} OemStage;
static OemStage SYSTEM_RAM start_stage[2], fire_stage[2];
static u16 SYSTEM_RAM active_fire_fraction[2], active_fire_counter[2], active_fire_at[2];
static u8 SYSTEM_RAM active_fire_timed[2], fire_pending[2], time_fire[2];
static u16 SYSTEM_RAM time_fire_ticks[2];
static OemIgnition SYSTEM_RAM oem;          /* the ROM's scheduler state, on-chip as F7xx */
static u32 oem_boundary_stamp;
static u16 oem_boundary_counter, oem_boundary_epoch, oem_state_epoch;
static u8 oem_boundary_tooth, oem_boundary_seen, oem_state_seen;
static u16 next_dwell[2];                    /* plan-derived pass inputs (oem_prepare) */
static s16 next_advance;
static u8 oem_prepared;
static u16 SYSTEM_RAM dwell_base[2];
volatile u8 coil_phase[2];
volatile u16 coil_event_epoch[2];
static u16 SYSTEM_RAM claimed_cycle[4], claimed_epoch[4];
static u8 SYSTEM_RAM claimed[4];
static OemDwell SYSTEM_RAM dwell_state[2];
static volatile u16 SYSTEM_RAM charge_at[2], feedback_interval[2];
static volatile u8 SYSTEM_RAM feedback_owner, feedback_seen[2], feedback_ready[2], feedback_fallback[2];
static volatile u16 SYSTEM_RAM completed_interval[2];
static volatile u8 SYSTEM_RAM completed_missing[2], completed_fallback[2];
static volatile u8 SYSTEM_RAM feedback_bad[2], completed_bad[2];
static volatile u8 SYSTEM_RAM injector_phase[4];
static u16 SYSTEM_RAM injector_end[4], injector_epoch[4];
typedef struct {
    u32 clock;
    u16 at, epoch;
    u8 ready;
} InjectionEvent;
static InjectionEvent SYSTEM_RAM pending_injection[2];
static u32 SYSTEM_RAM injection_clock;
static u16 SYSTEM_RAM injection_at;
static u8 SYSTEM_RAM tach_active;
static void cancel_stages(void);
static void oem_prepare(void);
void hal_cancel_fuel(void) SHARED {
    CCM7 &= 0xF000U; CCM5 &= 0x0FFFU;
    CC30IR = CC29IR = CC28IR = CC23IR = 0;
    PIN_INJ_1 = PIN_INJ_4 = PIN_INJ_3 = PIN_INJ_2 = 1;
    injector_phase[0] = injector_phase[1] = injector_phase[2] = injector_phase[3] = 0;
    pending_injection[0].ready = pending_injection[1].ready = 0;
}
void hal_cancel_spark(void) SHARED {
    CCM0 &= 0xFF00U; CCM1 &= 0xF0F0U; CCM5 &= 0xFF00U;
    CC0IR = CC1IR = CC4IR = CC6IR = CC20IR = CC21IR = 0;
    CC9IE = 0; CC9IR = 0;
    PIN_COIL_A = PIN_COIL_B = 1;
    coil_phase[0] = coil_phase[1] = 0;
    cancel_stages();
    feedback_ready[0] = feedback_ready[1] = 0;
    ecu.authority.spark_draining = 0;
}
void hal_cancel_all(void) SHARED { hal_cancel_fuel(); hal_cancel_spark(); }
void board_outputs_init(void) {
    u8 i;
    hal_cancel_all(); tach_active = 0; feedback_owner = 2;
    memset(&oem, 0, sizeof(oem));
    oem_state_seen = oem_boundary_seen = oem_prepared = 0;
    for (i = 0; i < 4; i++) claimed[i] = 0;
    for (i = 0; i < 2; i++) {
        dwell_state[i].correction = 0; dwell_state[i].duration = 0;
        dwell_state[i].previous_fallback = 1;
        feedback_seen[i] = feedback_fallback[i] = 0;
    }
}
static void start_compare(u8 ch, u16 value, u8 timed) {
    if (ch == 0) {
        CC6IR = 0; CC6 = value;
        CCM1 = (CCM1 & 0xF0FFU) | (timed ? 0x0C00U : 0x0400U);
    } else {
        CC4IR = 0; CC4 = value;
        CCM1 = (CCM1 & 0xFFF0U) | (timed ? 12U : 4U);
    }
}
/* Coarse stage: interrupt-only compare on T0 (tooth count). Fine stage:
   compare mode 1 on T1 toggles CC0IO=P2.0 / CC1IO=P2.1 from charging (0) to
   off (1) at the exact compare time, so interrupt latency cannot delay the
   spark. The handler then disables the compare and repeats the pin write,
   covering a toggle lost to a simultaneous software write of the P2 latch.
   Every path that turns a coil off disables its compare first. */
static void fire_compare(u8 ch, u16 value, u8 timed) {
    if (ch == 0) {
        CCM0 &= 0xFFF0U; CC0IR = 0; CC0 = value;
        CCM0 = (CCM0 & 0xFFF0U) | (timed ? 0x0DU : 4U);
    } else {
        CCM0 &= 0xFF0FU; CC1IR = 0; CC1 = value;
        CCM0 = (CCM0 & 0xFF0FU) | (timed ? 0xD0U : 0x40U);
    }
}
static void fire_compare_off(u8 ch) {
    if (ch == 0) { CCM0 &= 0xFFF0U; CC0IR = 0; }
    else { CCM0 &= 0xFF0FU; CC1IR = 0; }
}
static void stop_start(u8 ch) {
    if (ch == 0) { CCM1 &= 0xF0FFU; CC6IR = 0; }
    else { CCM1 &= 0xFFF0U; CC4IR = 0; }
}
static void cancel_stages(void) {
    u8 ch;
    for (ch = 0; ch < 2U; ch++) {
        start_stage[ch].mode = fire_stage[ch].mode = STAGE_NONE;
        fire_pending[ch] = time_fire[ch] = 0;
    }
}
void hal_revoke_spark(u8 lost_angle) SHARED {
    u8 ch;
    CCM1 &= 0xF0F0U; CC4IR = CC6IR = 0;
    cancel_stages();
    feedback_ready[0] = feedback_ready[1] = 0;
    CC9IE = 0; CC9IR = 0; feedback_owner = 2;
    for (ch = 0; ch < 2U; ch++) {
        if (!ecu.authority.coil_active[ch]) { hal_coil_off(ch); continue; }
        ecu.authority.spark_draining |= (u8)(1U << ch);
        /* Retain an installed timer deadline. A coarse stage must not follow
           a now-untrusted tooth counter: use its predicted time deadline. */
        if (lost_angle && !active_fire_timed[ch]) {
            active_fire_timed[ch] = 1;
            fire_compare(ch, active_fire_at[ch], 1);
            if ((s16)(active_fire_at[ch] - T1) <= 12) {
                if (!ch) CC0IR = 1; else CC1IR = 1;
            }
        }
    }
}
static u8 event_claim(u8 id, u16 cycle) {
    if (claimed[id] && claimed_epoch[id] == ecu.authority.epoch && claimed_cycle[id] == cycle) return 0;
    claimed[id] = 1; claimed_cycle[id] = cycle; claimed_epoch[id] = ecu.authority.epoch;
    return 1;
}
/* Capture-anchored fine timing from the most recent PEC words, as in the
   OEM CC0/CC4/CC6 first stage. A noise interval revokes angle authority. */
static void late_event(void) {
    if (ecu.authority.late_events != 65535U) ecu.authority.late_events++;
}
/* OEM fine stage (CC6INT 3897E / CC4INT 38B1E, CC0INT 38944): the capture of
   the coarse tooth plus (last interval >> 3) x substeps; a charge on the CC6
   path subtracts its correction and keeps at least two substeps. Standalone
   guards only: a stage serviced one tooth late anchors to its own tooth's
   capture, and an implausible interval revokes angle. */
static u8 fine(u16 fraction, u16 correction, u8 full, u16 expected, u16 *at) {
    u16 capture, period, counter;
    u8 late = 0;
    if (!board_capture_snapshot(&counter, &capture, &period)) {
        late_event(); return 0;
    }
    if (counter != expected) {
        if ((u16)(counter - expected) != 1U ||
            (u32)period * 5UL > ecu.rotation.normal * 9UL) {
            late_event(); return 0;
        }
        capture = (u16)(capture - period);
        late = 1;
    }
    if (!period || (u32)period * 5UL < ecu.rotation.normal * 3UL ||
        (u32)period * 5UL > ecu.rotation.normal * 9UL) {
        safety_inhibit(INH_SYNC, ecu.milliseconds); return 0;
    }
    *at = (u16)(capture + oem_angle_refine(period, fraction, correction, full));
    if (late && (s16)(*at - T1) <= 12) {
        late_event(); return 0;
    }
    return 1;
}
/* Install a stored fire on a charging coil (hal_lock held). */
static void fire_install(u8 ch) {
    OemStage *f = &fire_stage[ch];
    if (f->mode == STAGE_NONE) return;
    if (f->epoch != ecu.authority.epoch) { f->mode = STAGE_NONE; fire_pending[ch] = 0; return; }
    if (!ecu.authority.coil_active[ch]) return;     /* installed at charge start */
    active_fire_at[ch] = f->predicted;
    if (f->mode == STAGE_COARSE) {
        active_fire_timed[ch] = 0;
        active_fire_counter[ch] = f->value;
        active_fire_fraction[ch] = f->fraction;
        fire_compare(ch, f->value, 0);
        if ((s16)(f->value - T0) <= 0) { if (!ch) CC0IR = 1; else CC1IR = 1; }
    } else {
        active_fire_timed[ch] = 1;
        fire_compare(ch, f->value, 1);
        if ((s16)(f->value - T1) <= 12) { if (!ch) CC0IR = 1; else CC1IR = 1; }
    }
    f->mode = STAGE_NONE;
}
u8 hal_coil_start(u8 ch, u16 ticks) SHARED {
    u16 watch;
    if (!BOARD_RELEASED || ch > 1U || ticks < 625U || ticks > 7500U) return 0;
    /* All admission checks precede the charge edge. */
    if (!ch) PIN_COIL_A = 0; else PIN_COIL_B = 0;
    charge_at[ch] = T1;
    ecu.authority.spark_draining &= (u8)~(1U << ch);
    watch = (u16)(T7 + 8125U);
    if (ch == 0) {
        CC20IR = 0; CC20 = watch; CCM5 = (CCM5 & 0xFFF0U) | 4U;
    } else {
        CC21IR = 0; CC21 = watch; CCM5 = (CCM5 & 0xFF0FU) | 0x40U;
    }
    /* Until the segment pass delivers this coil's angular fire, end the
       charge after its dwell: the ROM's time-fire form (CC0 = T1 + F7FC) when
       requested, otherwise the planned dwell. */
    active_fire_at[ch] = (u16)(charge_at[ch] + (time_fire[ch] ? time_fire_ticks[ch] : ticks));
    active_fire_timed[ch] = 1;
    fire_compare(ch, active_fire_at[ch], 1);
    if ((s16)(active_fire_at[ch] - T1) <= 12) { if (!ch) CC0IR = 1; else CC1IR = 1; }
    if (time_fire[ch]) { time_fire[ch] = 0; fire_stage[ch].mode = STAGE_NONE; fire_pending[ch] = 1; }
    feedback_seen[ch] = feedback_bad[ch] = 0;
    feedback_fallback[ch] = (u8)(ecu.authority.coil_active[1U - ch] ||
                                ecu.control.mode != ENGINE_RUNNING || !ecu.authority.plan.dwell_feedback);
    if (!feedback_fallback[ch]) {
        feedback_owner = ch; CC9IR = 0; CC9IE = 1;
    } else { CC9IE = 0; feedback_owner = 2; }
    coil_phase[ch] = 2;
    return 1;
}
void hal_coil_off(u8 ch) SHARED {
    if (ch == 0) {
        CCM0 &= 0xFFF0U; PIN_COIL_A = 1; CCM5 &= 0xFFF0U;
        CC0IR = CC20IR = 0;
    } else {
        CCM0 &= 0xFF0FU; PIN_COIL_B = 1; CCM5 &= 0xFF0FU;
        CC1IR = CC21IR = 0;
    }
    coil_phase[ch] = 0;
    ecu.authority.spark_draining &= (u8)~(1U << ch);
}
u8 hal_injector_start(u8 ch, u16 ticks) SHARED {
    s32 remaining = (s32)(injection_clock - hal_capture_clock());
    if (!BOARD_RELEASED || ch > 3U || ticks < 25U || ticks > 31250U ||
        remaining < 64L || remaining > 30000L) return 0;
    injector_epoch[ch] = ecu.authority.epoch;
    injector_end[ch] = (u16)(injection_at + ticks);
    injector_phase[ch] = 1;
    switch (ch) {
    case 0:
        CC30IR = 0; CC30 = injection_at; CCM7 = (CCM7 & 0xF0FFU) | 0x500U; break;
    case 1:   /* cylinder 4: CC28IO = P7.4 */
        CC28IR = 0; CC28 = injection_at; CCM7 = (CCM7 & 0xFFF0U) | 5U; break;
    case 2:   /* cylinder 3: CC29IO = P7.5 */
        CC29IR = 0; CC29 = injection_at; CCM7 = (CCM7 & 0xFF0FU) | 0x50U; break;
    default:
        CC23IR = 0; CC23 = injection_at; CCM5 = (CCM5 & 0x0FFFU) | 0x5000U; break;
    }
    if ((s16)(injection_at - T7) <= 0) { hal_cancel_fuel(); return 0; }
    return 1;
}
/* Called with interrupts masked. Keep one future start per pair while its
   current pulse owns the compares. Re-admit with the current coherent plan
   after both end ISRs: cuts, freshness, epoch and soft-cut accounting still
   pass through the shared safety owner. A late start is an explicit fault. */
static void injection_pending(u8 pair) {
    InjectionEvent *e = &pending_injection[pair];
    u8 ch = (u8)(pair * 2U);
    if (!e->ready || ecu.authority.injector_active[ch] || ecu.authority.injector_active[ch + 1U]) return;
    e->ready = 0;
    if (ecu.authority.inhibits || e->epoch != ecu.authority.epoch) return;
    injection_clock = e->clock; injection_at = e->at;
    if ((s32)(injection_clock - hal_capture_clock()) < 64L) {
        safety_inhibit(INH_DEADLINE, ecu.milliseconds); return;
    }
    injector_admit(pair);
}
/* Run once per delivered capture block in XP1. Start descriptors are separate
   from an already charging coil, allowing overlapping dwell at high speed. */
/* Injection (standalone) and the tach, once per delivered capture block in XP1. */
void board_schedule(u32 captured) {
    u16 target, counts, cycle, lock, reference;
    u32 delay, now;
    u8 ch;
    if (ecu.rotation.state != ROT_VALID || ecu.authority.inhibits) return;
    now = hal_capture_clock(); reference = T7;
    if (now - captured > ecu.rotation.normal * 30UL) {
        safety_inhibit(INH_DEADLINE, ecu.milliseconds); return;
    }
    reference = (u16)(reference - (u16)(now - captured));
    for (ch = 0; ch < 2; ch++) {
        target = (u16)(ecu.authority.plan.injection_phase10 / 60U + (ch ? 30U : 0U));
        cycle = ecu.rotation.cycle;
        if (target < ecu.rotation.tooth) cycle++;
        counts = target >= ecu.rotation.tooth ? target - ecu.rotation.tooth : target + 60U - ecu.rotation.tooth;
        delay = ecu.rotation.normal * counts;
        if (delay > 30000UL || (s32)(captured + delay - hal_capture_clock()) < 128L) continue;
        lock = hal_lock();
        /* Recheck inside the same exclusion as publication/admission. An
           intervening higher-priority handler cannot consume this margin. */
        if ((s32)(captured + delay - hal_capture_clock()) < 128L) {
            late_event(); hal_unlock(lock); continue;
        }
        if (!ecu.authority.inhibits && event_claim((u8)(ch + 2U), cycle)) {
            if (pending_injection[ch].ready) {
                safety_inhibit(INH_DEADLINE, ecu.milliseconds);
            } else {
                pending_injection[ch].clock = captured + delay;
                pending_injection[ch].at = (u16)(reference + (u16)delay);
                pending_injection[ch].epoch = ecu.authority.epoch;
                pending_injection[ch].ready = 1;
                injection_pending(ch);
            }
        }
        hal_unlock(lock);
    }
    oem_prepare();
    if ((ecu.rotation.tooth == 1U || ecu.rotation.tooth == 31U) && !tach_active) {
        PIN_TACH = 1; tach_active = 1; CC27IR = 0; CC27 = (u16)(T7 + 3750U);
        CCM6 = (CCM6 & 0x0FFFU) | 0x4000U;
    }
}
static u16 dwell_ticks(u8 ch, u16 unit) {
    u16 ticks, lock, measured;
    u8 ready, missing, fallback, bad;
    ticks = us_ticks(ecu.authority.plan.dwell_us);
    if (ticks < 625U || ticks > 7500U) return 0;
    lock = hal_lock();
    ready = feedback_ready[ch]; measured = completed_interval[ch];
    missing = completed_missing[ch]; fallback = completed_fallback[ch];
    bad = completed_bad[ch];
    feedback_ready[ch] = 0;
    hal_unlock(lock);
    if (!ecu.authority.plan.dwell_feedback || (ready && (missing || bad))) {
        /* Missing/unqualified feedback cannot increase charge time.
           Re-qualify from calibrated dwell when valid feedback returns. */
        dwell_state[ch].correction = 0; dwell_state[ch].duration = 0;
        dwell_state[ch].previous_fallback = 1;
        if (ready && missing && !bad && !fallback && ecu.authority.plan.dwell_feedback &&
            ecu.authority.feedback_missing[ch] != 65535U) ecu.authority.feedback_missing[ch]++;
    } else if (ready) {
        oem_dwell_stock_update(&dwell_state[ch], ticks, measured, 0, fallback, 0);
        dwell_base[ch] = ticks;
    }
    ecu.authority.feedback_correction[ch] = dwell_state[ch].correction;
    if (dwell_state[ch].duration) {
        s32 corrected = (s32)ticks + dwell_state[ch].duration - dwell_base[ch];
        ticks = (u16)clamp32(corrected, 625L, 7500L);
    }
    /* Board admission bound: each coil must have an off interval before
       its following revolution. Standalone limit, not an OEM calibration. */
    if ((u32)ticks > (u32)unit * 464UL) ticks = (u16)(unit * 464U);
    return ticks;
}
/* Charge now: the ROM's CC6INT/CC4INT timed stage. Only the CC6 path hands
   the fire to charge time when FD1C.10 or FD1C.13 ask for it (38A80). */
static void charge(u8 ch, u8 cc6, u16 epoch) {
    u16 lock = hal_lock();
    if (cc6 && (oem.fd1c & 0x2400U)) {
        oem.fd1c &= ~0x2000U;
        time_fire[ch] = 1; time_fire_ticks[ch] = oem.f7fc;
    }
    hal_unlock(lock);
    coil_admit(ch, epoch, (u16)(((u32)oem.dwell[ch] * 4UL) / 5UL));
    lock = hal_lock(); time_fire[ch] = 0; hal_unlock(lock);
}
/* Hook: the ROM set IR with IE on a compare while its pass runs at low
   priority, so that handler runs at once and clears IE at its end. */
static void oem_now(OemIgnition *s, u8 compare) {
    u16 lock;
    u8 ch;
    if (compare == OEM_IGN_CHARGES_OFF) {                /* 38036 / 37D7A */
        lock = hal_lock();
        for (ch = 0; ch < 2U; ch++) { stop_start(ch); start_stage[ch].mode = STAGE_NONE; }
        s->p2 = (u16)(P2 & 3U);
        hal_unlock(lock);
    } else if (compare == 0U) {                          /* CC0INT: fire F7FE */
        ch = (u8)(s->f7fe & 1U);
        s->cc0ic &= ~(OEM_IGN_IE | OEM_IGN_IR);
        lock = hal_lock();
        fire_stage[ch].mode = STAGE_NONE; fire_pending[ch] = 0;
        if (ecu.authority.coil_active[ch]) {
            active_fire_timed[ch] = 1;
            fire_compare(ch, T1, 1);
            if (!ch) CC0IR = 1; else CC1IR = 1;
        }
        hal_unlock(lock);
    } else if (compare == 6U) {                          /* CC6INT: charge F7F6 */
        ch = (u8)(s->f7f6 & 1U);
        s->cc6ic &= ~(OEM_IGN_IE | OEM_IGN_IR);
        if (!ecu.authority.plan.spark_cut) charge(ch, 1, ecu.authority.epoch);
    } else {                                             /* CC4INT: charge F7F4 */
        ch = (u8)(s->f7f4 & 1U);
        s->cc4ic &= ~(OEM_IGN_IE | OEM_IGN_IR);
        if (!ecu.authority.plan.spark_cut) charge(ch, 0, ecu.authority.epoch);
    }
}
static u16 read_t1(void) { return T1; }
static void start_install(u8 ch, u16 value, u8 timed, u8 fraction, u16 correction, u8 full, u8 cc6) {
    OemStage *st = &start_stage[ch];
    st->value = value; st->fraction = fraction; st->correction = correction;
    st->full = full; st->cc6 = cc6; st->epoch = ecu.authority.epoch;
    st->mode = timed ? STAGE_TIMED : STAGE_COARSE;
    coil_event_epoch[ch] = st->epoch;
    start_compare(ch, value, timed);
    if (timed ? (s16)(value - T1) <= 12 : (s16)(value - T0) <= 0) {
        if (!ch) CC6IR = 1; else CC4IR = 1;
    }
}
/* Plan-derived pass inputs, computed after each block decode (outside the
   boundary's critical path): dwell per coil and the ROM advance byte. The
   foreground republishes the plan every 10 ms; the ROM likewise computes
   advance in a separate task. */
static void oem_prepare(void) {
    u16 unit = (u16)(ecu.rotation.normal >> 3);
    s16 position;
    if (!unit) return;
    next_dwell[0] = dwell_ticks(0, unit); next_dwell[1] = dwell_ticks(1, unit);
    /* Fire position from the tune: trigger10 - advance10 (0.1 deg) after the
       first tooth past the gap, which is 8 counts before the tooth-1 boundary.
       The ROM places TDC at 0x90 counts, so advance = 152 - position. */
    position = (s16)(ecu.authority.plan.trigger10 - ecu.authority.plan.advance10); /* |x| < 8192 */
    position = (s16)((position * 4 + (position >= 0 ? 15 : -15)) / 30);
    position = (s16)(152 - position);
    next_advance = position; /* Apply global retard before the final clamp. */
    oem_prepared = 1;
}
/* One ROM segment pass at a boundary capture (tooth 1 or 31), before the
   block is decoded. Inputs are converted to the ROM's units: 0.75-degree
   counts, TDC 144 counts after the boundary, T1 ticks. */
void board_ignition_segment(u8 tooth, u32 stamp, u16 counter, u16 interval) {
    u32 now, period;
    u16 lock, unit;
    u8 ch, valid, i;
    if (ecu.rotation.state != ROT_VALID || ecu.authority.inhibits) { oem_boundary_seen = 0; return; }
    now = hal_capture_clock();
    if (now - stamp > ecu.rotation.normal * 30UL) {
        safety_inhibit(INH_DEADLINE, ecu.milliseconds); return;
    }
    unit = (u16)(ecu.rotation.normal >> 3);
    if (!unit) return;
    if (!oem_state_seen || oem_state_epoch != ecu.rotation.epoch) {
        u16 f800 = oem.f800, f802 = oem.f802;   /* running maxima since boot */
        memset(&oem, 0, sizeof(oem));
        oem.f800 = f800; oem.f802 = f802;
        oem.fd1c = 0x0900U;                      /* FD1C.11 synchronized, FD1C.8 fire from F7E4 */
        for (i = 0; i < 4U; i++) { oem.mask[i] = (u8)((i & 1U) ? 2U : 1U); oem.index[i] = (u8)(i & 1U); }
        oem_state_seen = 1; oem_state_epoch = ecu.rotation.epoch;
    }
    period = stamp - oem_boundary_stamp;
    valid = (u8)(oem_boundary_seen && oem_boundary_epoch == ecu.rotation.epoch &&
                 oem_boundary_tooth != tooth &&
                 (u16)(counter - oem_boundary_counter) == (tooth == 1U ? 28U : 30U) &&
                 period && period <= 0xFFFFFFUL);
    oem.f7aa = (u16)stamp; oem.f7a6 = counter;
    oem.f8d1 = oem.f829 = (u8)(tooth == 1U ? 0U : 1U);
    oem.f8d2 = 3;
    oem.fd6a = (u16)(tooth == 31U ? 0x0008U : 0U);       /* FD6A.3: gap segment starts */
    /* Before a measured segment the ROM's time base is the last tooth (9500 < 3);
       its segment period is still defined. Estimate it as 30 tooth periods. */
    oem.r9500 = (u8)(valid ? 5U : 0U);
    if (!valid) period = ecu.rotation.normal * 30UL;
    if (period > 0xFFFFFFUL) period = 0xFFFFFFUL;
    oem.f8ad = (u8)(period >> 16);
    oem.f8b0 = (u16)period;
    /* F8AE (rpm x 4 = 150e6 / period) is used only against 300 (37D6A). */
    oem.f8ae = (u16)(period <= 500000UL ? 0xFFFFU : 0U);
    oem.cap_prev = 0; oem.cap_last = interval;
    oem.r82dc = oem.r9716 = 0;
    oem.p2 = (u16)(P2 & 3U);
    if (!oem_prepared) oem_prepare();
    oem.dwell[0] = next_dwell[0]; oem.dwell[1] = next_dwell[1];
    ch = (u8)(s8)knock_advance(next_advance);
    for (i = 0; i < 4U; i++) oem.adv[i] = ch;
    lock = hal_lock();
    oem.ccm0 = oem.ccm1 = 0; oem.cc4ic = oem.cc6ic = 0;
    oem.cc0ic = (u16)(fire_pending[oem.f7fe & 1U] ? OEM_IGN_IE : 0U);
    hal_unlock(lock);
    oem_ignition_clock = read_t1;
    oem_ignition_now = oem_now;
    oem_ignition_segment(&oem);
    lock = hal_lock();
    if (!ecu.authority.inhibits) {
        if ((oem.cc0ic & (OEM_IGN_IE | OEM_IGN_IR)) == OEM_IGN_IE) {
            OemStage *f;
            ch = (u8)(oem.f7fe & 1U);
            f = &fire_stage[ch];
            f->epoch = ecu.authority.epoch; f->fraction = oem.f7f0; f->correction = 0; f->full = 0;
            if (oem.ccm0 & OEM_IGN_CCM0_CC0_T1) {
                f->mode = STAGE_TIMED; f->value = f->predicted = oem.cc0;
            } else {
                f->mode = STAGE_COARSE; f->value = oem.cc0;
                f->predicted = (u16)(oem.f7aa + (u16)(((u16)oem.f7f1 * 8U + oem.f7f0) * oem.f7fa));
            }
            fire_pending[ch] = 1;
            fire_install(ch);
        } else if (oem.f8ae >= 300U && !(oem.cc0ic & OEM_IGN_IE)) {
            ch = (u8)(oem.f7fe & 1U);                     /* 381A4 cancelled the angle fire */
            if (!ecu.authority.coil_active[ch]) fire_stage[ch].mode = STAGE_NONE;
        }
        if ((oem.cc6ic & (OEM_IGN_IE | OEM_IGN_IR)) == OEM_IGN_IE)
            start_install((u8)(oem.f7f6 & 1U), oem.cc6, (u8)((oem.ccm1 & OEM_IGN_CCM1_CC6_T1) != 0U),
                          oem.f7df, oem.r971a[oem.f7f6 & 1U], 1, 1);
        if ((oem.cc4ic & (OEM_IGN_IE | OEM_IGN_IR)) == OEM_IGN_IE)
            start_install((u8)(oem.f7f4 & 1U), oem.cc4, (u8)((oem.ccm1 & OEM_IGN_CCM1_CC4_T1) != 0U),
                          oem.f7e0, 0, 0, 0);
    }
    hal_unlock(lock);
    oem_boundary_stamp = stamp; oem_boundary_counter = counter;
    oem_boundary_epoch = ecu.rotation.epoch; oem_boundary_tooth = tooth; oem_boundary_seen = 1;
}
static void dwell_event(u8 ch) {
    OemStage *st = &start_stage[ch];
    u16 at, epoch;
    u8 cc6;
    if (st->mode == STAGE_NONE || ecu.authority.inhibits || st->epoch != ecu.authority.epoch ||
        ecu.authority.plan.spark_cut) {
        stop_start(ch); st->mode = STAGE_NONE; return;
    }
    if (st->mode == STAGE_COARSE) {
        if (!fine(st->fraction, st->correction, st->full, st->value, &at)) {
            stop_start(ch); st->mode = STAGE_NONE; return;
        }
        st->mode = STAGE_TIMED; start_compare(ch, at, 1);
        if ((s16)(at - T1) <= 12) { if (!ch) CC6IR = 1; else CC4IR = 1; }
        return;
    }
    stop_start(ch);
    epoch = st->epoch; cc6 = st->cc6;
    st->mode = STAGE_NONE;
    charge(ch, cc6, epoch);
    fire_install(ch);
}
static void fire_event(u8 ch) {
    u16 at;
    if (!active_fire_timed[ch] && ecu.authority.coil_active[ch]) {
        if (!fine(active_fire_fraction[ch], 0, 0, active_fire_counter[ch], &at)) {
            if (!ecu.authority.coil_active[ch]) return;
            at = active_fire_at[ch];
        }
        active_fire_timed[ch] = 1; fire_compare(ch, at, 1);
        if ((s16)(at - T1) <= 12) { if (!ch) CC0IR = 1; else CC1IR = 1; }
        return;
    }
    /* The compare toggle has already ended the charge at its exact time.
       Disable the compare (a second match must never toggle it back on), then
       repeat the pin write as the OEM edge stub does, covering a lost toggle
       or a software-requested late stage. */
    fire_compare_off(ch);
    if (!ch) PIN_COIL_A = 1; else PIN_COIL_B = 1;
    fire_pending[ch] = 0;
    if (feedback_owner == ch) { CC9IE = 0; feedback_owner = 2; }
    completed_interval[ch] = feedback_interval[ch];
    completed_missing[ch] = (u8)!feedback_seen[ch];
    completed_fallback[ch] = feedback_fallback[ch];
    completed_bad[ch] = feedback_bad[ch];
    feedback_ready[ch] = (u8)(ecu.authority.coil_active[ch] &&
                              !(ecu.authority.spark_draining & (1U << ch)));
    coil_done(ch);
}
void coil0_isr(void) IRQ_HANDLER(0x16) {
    u16 lock = hal_lock(); dwell_event(0); hal_unlock(lock);
}
void coil1_isr(void) IRQ_HANDLER(0x14) {
    u16 lock = hal_lock(); dwell_event(1); hal_unlock(lock);
}
void spark0_isr(void) IRQ_HANDLER(0x10) {
    u16 lock = hal_lock(); fire_event(0); hal_unlock(lock);
}
void spark1_isr(void) IRQ_HANDLER(0x11) {
    u16 lock = hal_lock(); fire_event(1); hal_unlock(lock);
}
void dwell_feedback_isr(void) IRQ_HANDLER(0x19) {
    u16 lock = hal_lock();
    CC9IE = 0;
    if (feedback_owner < 2U) {
        u16 interval = (u16)(CC9 - charge_at[feedback_owner]);
        u16 elapsed = (u16)(T1 - charge_at[feedback_owner]);
        if (ecu.authority.coil_active[feedback_owner] && interval && interval <= elapsed) {
            feedback_interval[feedback_owner] = interval;
            feedback_seen[feedback_owner] = 1;
        } else {
            feedback_bad[feedback_owner] = 1;
            if (ecu.authority.feedback_invalid[feedback_owner] != 65535U)
                ecu.authority.feedback_invalid[feedback_owner]++;
        }
    }
    hal_unlock(lock);
}
static u8 injector_event(u8 ch) {
    if (injector_phase[ch] == 1U) {
        if (ecu.authority.inhibits || injector_epoch[ch] != ecu.authority.epoch ||
            ecu.authority.plan.fuel_cut || (s16)(injector_end[ch] - T7) <= 12) {
            safety_inhibit(INH_OUTPUT, ecu.milliseconds); return 0;
        }
        injector_phase[ch] = 2;
        return 1;
    }
    injector_phase[ch] = 0; injector_done(ch); return 0;
}
void injector0_isr(void) IRQ_HANDLER(0x45) {
    u16 lock = hal_lock();
    if (injector_event(0)) CC30 = injector_end[0];
    else { CCM7 &= 0xF0FFU; PIN_INJ_1 = 1; }
    if (!injector_phase[0]) injection_pending(0);
    hal_unlock(lock);
}
void injector1_isr(void) IRQ_HANDLER(0x3C) {
    u16 lock = hal_lock();
    if (injector_event(1)) CC28 = injector_end[1];
    else { CCM7 &= 0xFFF0U; PIN_INJ_4 = 1; }
    if (!injector_phase[1]) injection_pending(0);
    hal_unlock(lock);
}
void injector2_isr(void) IRQ_HANDLER(0x44) {
    u16 lock = hal_lock();
    if (injector_event(2)) CC29 = injector_end[2];
    else { CCM7 &= 0xFF0FU; PIN_INJ_3 = 1; }
    if (!injector_phase[2]) injection_pending(1);
    hal_unlock(lock);
}
void injector3_isr(void) IRQ_HANDLER(0x37) {
    u16 lock = hal_lock();
    if (injector_event(3)) CC23 = injector_end[3];
    else { CCM5 &= 0x0FFFU; PIN_INJ_2 = 1; }
    if (!injector_phase[3]) injection_pending(1);
    hal_unlock(lock);
}
void watchdog0_isr(void) IRQ_HANDLER(0x34) { safety_inhibit(INH_OUTPUT, ecu.milliseconds); }
void watchdog1_isr(void) IRQ_HANDLER(0x35) { safety_inhibit(INH_OUTPUT, ecu.milliseconds); }
void tach_isr(void) IRQ_HANDLER(0x3B) {
    u16 lock = hal_lock(); CCM6 &= 0x0FFFU; PIN_TACH = 0; tach_active = 0; hal_unlock(lock);
}
