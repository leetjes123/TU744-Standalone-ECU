#include "ecu.h"
#include "diagnostic_monitors.h"
#include "lifecycle.h"
#include "knock.h"
#include <string.h>
u8 safety_aux_permitted(void) SHARED {
    u16 lock = hal_lock();
    u8 allowed = (u8)(ecu.board_released && ecu.cal.valid && ecu.key_input &&
        !ecu.service && !(ecu.authority.inhibits & AUX_SHUTDOWN_INHIBITS));
    hal_unlock(lock);
    return allowed;
}
void safety_inhibit(u16 reason, u32 now) SHARED {
    u16 lock = hal_lock();
    Authority *a = &ecu.authority;
    if ((a->inhibits & reason) != reason) {
        a->epoch++;
        a->faults++;
        if (!a->first_reason) {
            a->first_reason = reason;
            a->first_time = now;
        }
    }
    a->inhibits |= reason;
    a->generation++;
    knock_invalidate(0);
    if (reason & (INH_DEADLINE ^ 0xFFFFU)) {
        hal_cancel_fuel();
        a->injector_active[0] = a->injector_active[1] = a->injector_active[2] =
            a->injector_active[3] = 0;
    } else {
        /* A late tick or foreground pass: no new pulse starts, but one
           already open ends at its own compare, as a charging coil fires.
           Cutting it mid-pulse lost that cylinder's fuel for a timing hiccup. */
        hal_revoke_fuel();
    }
    if (reason & (INH_OUTPUT | INH_BOARD)) {
        hal_cancel_spark();
        a->coil_active[0] = a->coil_active[1] = 0;
    } else {
        /* Stop new charges; an existing charge owns its fire/watchdog until
           completion. Loss of angle uses its retained time deadline. */
        hal_revoke_spark((u8)((reason & (INH_SYNC | INH_DEADLINE)) || ecu.rotation.state != ROT_VALID));
    }
    hal_unlock(lock);
}
/* An inhibit with the check that tripped it, for the live frame. */
void safety_trip(u16 reason, u8 source, u32 now) SHARED {
    u16 lock = hal_lock();
    ecu.authority.trip_source = source;
    safety_inhibit(reason, now);
    hal_unlock(lock);
}
void safety_conditions(u16 reasons, u32 now) {
    u16 lock = hal_lock();
    /* Service, board and output-driver faults latch: service_poll clears a
       clean tool session, latch_poll an output latch after a stall, others
       need a reset. DEADLINE and every other condition recover only through
       this complete, coherent foreground evaluation. */
    reasons |= ecu.authority.inhibits & INH_LATCHED;
    if (reasons)
        safety_inhibit(reasons, now);
    if (!reasons && ecu.authority.inhibits)
        ecu.authority.epoch++;
    ecu.authority.inhibits = reasons;
    ecu.authority.generation++;
    hal_unlock(lock);
}
u8 safety_publish(const EnginePlan *p) SHARED {
    u16 lock = hal_lock();
    Authority *a = &ecu.authority;
    if (a->inhibits || p->epoch != a->epoch) {
        hal_unlock(lock);
        return 0;
    }
    if (p->fuel_cut) {
        hal_cancel_fuel();
        a->injector_active[0] = a->injector_active[1] = a->injector_active[2] =
            a->injector_active[3] = 0;
    }
    if (p->spark_cut) {
        hal_revoke_spark(0);
    }
    a->plan = *p;
    a->generation++;
    hal_unlock(lock);
    return 1;
}
static u8 permitted(u8 spark) SHARED {
    return (u8)(!ecu.authority.inhibits && ecu.authority.plan.epoch == ecu.authority.epoch && ecu.rotation.state == ROT_VALID &&
                ecu.milliseconds - ecu.authority.plan.stamp <= ecu.authority.plan.max_age_ms &&
                !(spark ? ecu.authority.plan.spark_cut : ecu.authority.plan.fuel_cut));
}
static void late_event(void) SHARED {
    if (ecu.authority.late_events != 65535U)
        ecu.authority.late_events++;
}
/* Soft-cut accounting and pair start; hal_lock held, admission checked.
   Like the OEM, a pulse longer than one compare range is clamped and a late
   start skips this event; only a refused driver latches. */
static u8 injector_start_pair(u8 pair, u16 ticks) SHARED {
    u8 ch = (u8)(pair * 2U), first, second = 0;
    ecu.authority.fuel_accumulator[pair] += ecu.authority.plan.soft_fuel;
    if (ecu.authority.fuel_accumulator[pair] >= 100) {
        ecu.authority.fuel_accumulator[pair] -= 100;
        return 0;
    }
    if (ticks > INJECTOR_MAX_TICKS) {
        ticks = INJECTOR_MAX_TICKS;
        if (ecu.authority.injector_clamps != 65535U)
            ecu.authority.injector_clamps++;
    }
    first = hal_injector_start(ch, ticks);
    if (first == 1U) {
        second = hal_injector_start((u8)(ch + 1), ticks);
        if (second != 1U)
            hal_injector_off(ch);
    }
    if (!first || (first == 1U && !second)) {
        safety_trip(INH_OUTPUT, TRIP_INJECTOR_REFUSED, ecu.milliseconds);
        return 0;
    }
    if (second != 1U) {
        late_event();
        return 0;
    }
    ecu.authority.injector_active[ch] = ecu.authority.injector_active[ch + 1] = 1;
    return 1;
}
u8 injector_admit(u8 pair) SHARED {
    u16 lock = hal_lock();
    u8 ch = (u8)(pair * 2U), result = 0;
    if (pair <= 1 && permitted(0) && ecu.authority.plan.pulse_us >= 20 && !ecu.authority.injector_active[ch] &&
        !ecu.authority.injector_active[ch + 1])
        result = injector_start_pair(pair, us_ticks(ecu.authority.plan.pulse_us));
    hal_unlock(lock);
    return result;
}
/* Without hal_lock: everything permitted(0) derives from inhibits, epoch,
   plan and rotation. Authority fields are volatile, so they are read after
   the generation; a writer preempting this changes the generation, and a
   loss of ROT_VALID always passes through safety_inhibit. */
u8 injector_prepare(InjectorTicket *t) SHARED {
    const volatile Authority *a = &ecu.authority;
    t->generation = a->generation;
    t->ok = (u8)(!a->inhibits && a->plan.epoch == a->epoch && ecu.rotation.state == ROT_VALID &&
                 !a->plan.fuel_cut && a->plan.pulse_us >= 20);
    t->ticks = us_ticks(a->plan.pulse_us);
    return t->ok;
}
/* Same admission as injector_admit for a ticket of the current generation:
   only plan age and the pair's activity can change without a generation
   step. Any other ticket takes the full injector_admit path. */
u8 injector_commit(u8 pair, const InjectorTicket *t) SHARED {
    u16 lock;
    u8 ch = (u8)(pair * 2U), result = 0;
    if (!t->ok || pair > 1)
        return injector_admit(pair);
    lock = hal_lock();
    if (t->generation != ecu.authority.generation) {
        hal_unlock(lock);
        return injector_admit(pair);
    }
    if (ecu.milliseconds - ecu.authority.plan.stamp <= ecu.authority.plan.max_age_ms &&
        !ecu.authority.injector_active[ch] && !ecu.authority.injector_active[ch + 1])
        result = injector_start_pair(pair, t->ticks);
    hal_unlock(lock);
    return result;
}
u8 coil_admit(u8 ch, u16 epoch, u16 dwell_us) SHARED {
    u16 lock = hal_lock();
    u8 result;
    if (ch > 1 || !permitted(1) || epoch != ecu.authority.epoch || ecu.authority.coil_active[ch] || dwell_us < 500 ||
        dwell_us > 6000) {
        hal_unlock(lock);
        return 0;
    }
    ecu.authority.spark_accumulator[ch] += ecu.authority.plan.soft_spark;
    if (ecu.authority.spark_accumulator[ch] >= 100) {
        ecu.authority.spark_accumulator[ch] -= 100;
        hal_unlock(lock);
        return 0;
    }
    result = hal_coil_start(ch, us_ticks(dwell_us));
    if (result != 1U) {
        if (!result) safety_trip(INH_OUTPUT, TRIP_COIL_REFUSED, ecu.milliseconds);
        hal_unlock(lock);
        return 0;
    }
    ecu.authority.coil_epoch[ch] = epoch;
    ecu.authority.coil_active[ch] = 1;
    hal_unlock(lock);
    return 1;
}
void injector_done(u8 ch) SHARED {
    if (ch < 4)
        ecu.authority.injector_active[ch] = 0;
}
/* Coil watchdog, after the board dropped that coil's stages: the coil is
   switched off, losing at most this spark. Repeated trips on one coil
   while running mean a stuck driver or an overheating coil and latch
   INH_OUTPUT. No OEM reference: the ROM has no dwell timeout (CC9 dwell
   regulator 38BDC..38D5C); this latch only protects the hardware. */
void coil_timeout(u8 ch) SHARED {
    u16 lock = hal_lock();
    if (ch < 2) {
        hal_coil_off(ch);
        ecu.authority.coil_active[ch] = 0;
        if (ecu.authority.coil_timeouts != 65535U)
            ecu.authority.coil_timeouts++;
        if (ecu.control.mode != ENGINE_RUNNING)
            ecu.authority.coil_timeout_run[ch] = 0;
        else if (++ecu.authority.coil_timeout_run[ch] >= COIL_WATCHDOG_LATCH)
            safety_trip(INH_OUTPUT, TRIP_COIL_WATCHDOG, ecu.milliseconds);
    }
    hal_unlock(lock);
}
void coil_done(u8 ch) SHARED {
    u16 lock = hal_lock();
    if (ch < 2) {
        ecu.authority.coil_timeout_run[ch] = 0;
        if (ch == 0U && ecu.authority.coil_active[ch] &&
            ecu.authority.coil_epoch[ch] == ecu.authority.epoch &&
            ecu.rotation.state == ROT_VALID && !ecu.authority.inhibits) {
            diagnostic_phase_arm(hal_capture_clock(), ecu.rotation.epoch, ecu.rotation.tooth);
            hal_phase_arm();
        }
        hal_coil_off(ch);
        ecu.authority.coil_active[ch] = 0;
    }
    hal_unlock(lock);
}
u8 service_enter(u32 now) {
    u16 lock = hal_lock();
    /* Latch before any storage/update action. Rotation thereafter never rearms. */
    if (ecu.control.mode != ENGINE_STOPPED || ecu.rotation.state != ROT_UNSYNCED ||
        ecu.iac.state == IAC_HOMING || ecu.authority.coil_active[0] || ecu.authority.coil_active[1]) {
        hal_unlock(lock);
        return 0;
    }
    if (!ecu.service)
        ecu.service_clean = 1;
    ecu.service = 1;
    safety_inhibit(INH_SERVICE, now);
    hal_unlock(lock);
    iac_disable();
    return (u8)!ecu.iac.off_pending;
}
