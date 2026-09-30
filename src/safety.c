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
    knock_invalidate(0);
    hal_cancel_fuel();
    if (reason & (INH_OUTPUT | INH_BOARD)) {
        hal_cancel_spark();
        a->coil_active[0] = a->coil_active[1] = 0;
    } else {
        /* Stop new charges; an existing charge owns its fire/watchdog until
           completion. Loss of angle uses its retained time deadline. */
        hal_revoke_spark((u8)((reason & (INH_SYNC | INH_DEADLINE)) || ecu.rotation.state != ROT_VALID));
    }
    a->injector_active[0] = a->injector_active[1] = a->injector_active[2] = a->injector_active[3] =
        0;
    hal_unlock(lock);
}
void safety_conditions(u16 reasons, u32 now) {
    u16 lock = hal_lock();
    /* Service, deadline and actuator faults require reset. Other conditions
       recover only through this complete, coherent foreground evaluation. */
    reasons |= ecu.authority.inhibits & (INH_SERVICE | INH_DEADLINE | INH_OUTPUT | INH_BOARD);
    if (reasons)
        safety_inhibit(reasons, now);
    if (!reasons && ecu.authority.inhibits)
        ecu.authority.epoch++;
    ecu.authority.inhibits = reasons;
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
    hal_unlock(lock);
    return 1;
}
static u8 permitted(u8 spark) SHARED {
    return (u8)(!ecu.authority.inhibits && ecu.authority.plan.epoch == ecu.authority.epoch && ecu.rotation.state == ROT_VALID &&
                ecu.milliseconds - ecu.authority.plan.stamp <= ecu.authority.plan.max_age_ms &&
                !(spark ? ecu.authority.plan.spark_cut : ecu.authority.plan.fuel_cut));
}
u8 injector_admit(u8 pair) SHARED {
    u16 lock = hal_lock(), ticks;
    u8 ch = (u8)(pair * 2U);
    if (pair > 1 || !permitted(0) || ecu.authority.plan.pulse_us < 20 || ecu.authority.injector_active[ch] ||
        ecu.authority.injector_active[ch + 1]) {
        hal_unlock(lock);
        return 0;
    }
    ecu.authority.fuel_accumulator[pair] += ecu.authority.plan.soft_fuel;
    if (ecu.authority.fuel_accumulator[pair] >= 100) {
        ecu.authority.fuel_accumulator[pair] -= 100;
        hal_unlock(lock);
        return 0;
    }
    ticks = us_ticks(ecu.authority.plan.pulse_us);
    if (!hal_injector_start(ch, ticks) || !hal_injector_start((u8)(ch + 1), ticks)) {
        safety_inhibit(INH_OUTPUT, ecu.milliseconds);
        hal_unlock(lock);
        return 0;
    }
    ecu.authority.injector_active[ch] = ecu.authority.injector_active[ch + 1] = 1;
    hal_unlock(lock);
    return 1;
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
        if (!result) safety_inhibit(INH_OUTPUT, ecu.milliseconds);
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
void coil_done(u8 ch) SHARED {
    u16 lock = hal_lock();
    if (ch < 2) {
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
    ecu.service = 1;
    safety_inhibit(INH_SERVICE, now);
    hal_unlock(lock);
    iac_disable();
    return (u8)!ecu.iac.off_pending;
}
