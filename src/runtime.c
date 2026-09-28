#include "control.h"
#include "lifecycle.h"
#include "oem_runtime.h"
#include "faults.h"
#include <string.h>
Ecu ecu;
TimingHealth timing_health;
static u16 clamp16(u32 v) { return (u16)(v > 65535UL ? 65535UL : v); }
void ecu_init(u8 released) {
    memset(&ecu, 0, sizeof(ecu));
    memset(&timing_health, 0, sizeof(timing_health));
    faults_init();
    ecu.board_released = released;
    lifecycle_init();
    ecu.authority.inhibits = INH_SYNC | INH_CAL | (released ? 0 : INH_BOARD);
    ecu.authority.epoch = 1;
    /* Each pair has its own cut budget; stagger the two schedules. */
    ecu.authority.fuel_accumulator[1] = 50U;
    ecu.authority.spark_accumulator[1] = 50U;
    ecu.control.trim_q16 = 65536L;
    ecu.control.applied_trim = 1024;
    ecu.control.afterstart = 100;
    ecu.control.ae_percent = ecu.control.ae_peak = 100;
    hal_cancel_all();
}
void ecu_tick(void) SHARED {
    ecu_tick_elapsed(1);
}
void ecu_tick_elapsed(u16 elapsed_ms) SHARED {
    u32 now;
    u16 lock = hal_lock(); /* higher-priority readers must never see a torn 32-bit clock */
    ecu.milliseconds += elapsed_ms;
    now = ecu.milliseconds;
    hal_unlock(lock);
    if (elapsed_ms > timing_health.tick_max_ms)
        timing_health.tick_max_ms = elapsed_ms;
    if (!ecu.authority.inhibits && clamp16(now - ecu.authority.plan.stamp) > timing_health.plan_age_max_ms)
        timing_health.plan_age_max_ms = clamp16(now - ecu.authority.plan.stamp);
    if (elapsed_ms > 3U)
        safety_inhibit(INH_DEADLINE, now);
    if (now - ecu.foreground_stamp > 50UL)
        safety_inhibit(INH_DEADLINE, now);
    if (!ecu.authority.inhibits && now - ecu.authority.plan.stamp > ecu.authority.plan.max_age_ms)
        safety_inhibit(INH_STALE, now);
}
void controls_update(u32 now, const Rotation *r) {
    const u8 *c = cal_active();
    Controls *s = &ecu.control;
    EnginePlan p;
    u16 reasons = 0, lock, dt;
    dt = (u16)(now - s->last_control);
    s->last_control = now;
    s->key_on = ecu.key_input;
    if (s->key_on && !s->previous_key) {
        s->prime_until = now + (u32)c[0x5EC] * 100UL;
        iac_home(now);
    } else if (!s->key_on && s->previous_key) {
        s->prime_until = now;
        iac_disable();
    }
    s->previous_key = s->key_on;
    sensors_update(now);
    engine_state_update(now, r, c);
    gear_update(r);
    if (r->state != ROT_VALID)
        reasons |= INH_SYNC;
    if (!s->key_on)
        reasons |= INH_POWER;
    /* Sensor validity records standalone DTCs without replacing measurements.
       It does not revoke fuel or ignition authority. Calibration still must
       be valid before this control path is called. */
    memset(&p, 0, sizeof(p));
    p.rpm = r->rpm;
    p.stamp = now;
    p.generation = ecu.cal.generation;
    p.max_age_ms = get16(c + CAL_PLAN_AGE);
    fan_update(c);
    idle_update(now, dt, r, c);
    limits_update(now, r, c, &p);
    acceleration_update(now, dt, r->rpm, c);
    fuel_plan(now, r, c, &p);
    /* Recover conditions and publish the complete replacement atomically.
       Clearing an inhibit earlier exposes the obsolete plan to the timer,
       which can latch STALE and revoke this still-in-progress publication. */
    lock = hal_lock();
    if (ecu.rotation.epoch != r->epoch || ecu.rotation.state != ROT_VALID)
        reasons |= INH_SYNC;
    safety_conditions(reasons, now);
    p.epoch = ecu.authority.epoch;
    safety_publish(&p);
    hal_unlock(lock);
    auxiliary_update(now, r, c);
}
void ecu_poll(void) {
    u32 now, finished;
    u16 lock, count;
    u8 control_ran = 0;
    Rotation r;
    lock = hal_lock();
    now = ecu.milliseconds;
    hal_unlock(lock);
    cal_poll(now);
    if (now - ecu.foreground_stamp > 50UL)
        safety_inhibit(INH_DEADLINE, now);
    fault_set(FAULT_CAL, ecu.cal.valid ? 0 : 1, now);
    power_poll(now);
    if (now - ecu.control.last_control >= 10UL) {
        control_ran = 1;
        rotation_snapshot(&r);
        if (ecu.cal.valid)
            controls_update(now, &r);
        else {
            safety_inhibit(INH_CAL, now);
            ecu.control.last_control = now;
        }
        if (now - ecu.vss_stamp >= 500UL) {
            lock = hal_lock();
            count = ecu.vss_count;
            hal_unlock(lock);
            if (ecu.cal.valid)
                ecu.sensors.speed_kph =
                    (u16)((u32)(u16)(count - ecu.vss_previous) * 3600UL /
                          get16(cal_active() + CAL_VSS_PPM) / (now - ecu.vss_stamp));
            /* Absence of pulses is unknown, never an automatic launch permit. */
            ecu.sensors.vss_valid = (u8)(count != ecu.vss_previous);
            ecu.vss_previous = count;
            ecu.vss_stamp = now;
        }
    }
    /* Diagnostics run after plan publication. Running them first delayed
       every control pass by their duration; with the 10 ms control gate,
       plans could then be published ~30 ms apart under interrupt load and
       trip STALE. The board clock observes the free-running T1 of capture. */
    oem_runtime_poll(now, hal_capture_clock());
    iac_service(now);
    /* A calibration job step (several ms) runs only right after a release. */
    protocol_service(now, control_ran);
    storage_poll(now);
    lock = hal_lock();
    finished = ecu.milliseconds;
    if (timing_health.seen) {
        if (clamp16(finished - now) > timing_health.pass_max_ms)
            timing_health.pass_max_ms = clamp16(finished - now);
        if (clamp16(now - ecu.foreground_stamp) > timing_health.interval_max_ms)
            timing_health.interval_max_ms = clamp16(now - ecu.foreground_stamp);
    }
    timing_health.seen = 1;
    ecu.foreground_stamp = now;
    hal_unlock(lock);
    /* A running foreground cannot mask a stopped interrupt clock by repeatedly
       feeding the watchdog. At most one service per observed clock advance. */
    if (now != ecu.watchdog_stamp && finished - now <= 50UL) {
        /* An output/deadline latch keeps outputs inhibited until reset; it
           must not itself cause a running ECU reset. The watchdog supervises
           clock and bounded foreground progress, including while latched.
           A stuck clock, stuck foreground or repeatedly overlong pass still
           cannot feed it. NOR service has its own bounded SRAM worker. */
        ecu.watchdog_stamp = now;
        hal_watchdog_service();
    }
}
