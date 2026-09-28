#include "oem_runtime.h"
#include "oem_layout.h"
#include <string.h>
OemRuntime oem_runtime;

void oem_runtime_reset(void) {
    memset(&oem_runtime, 0, sizeof(oem_runtime));
}
void oem_runtime_changed(void) {
    oem_history_owner_changed(&oem_runtime.history);
}
void diagnostic_mil_update(u32 now) {
    Diagnostics *d = &ecu.diagnostics;
    if (!ecu.key_input) {
        d->mil_flash_active = d->mil_output = 0;
    } else if (d->mil_flashing_requested) {
        if (!d->mil_flash_active) {
            d->mil_flash_at = now;
            d->mil_flash_active = 1;
        }
        /* Standalone policy: immediate ON, then 1 Hz at 50 percent duty.
           Supplied Bosch PDF p18 calls for flashing, not this chosen rate. */
        d->mil_output = (u8)((now - d->mil_flash_at) % 1000UL < 500UL);
    } else {
        d->mil_flash_active = 0;
        d->mil_output = d->mil_steady;
    }
}
static void publish(u32 now) {
    u8 i;
    for (i = 0; i < 107U; i++)
        ecu.diagnostics.live[i] = oem_runtime.state.events.live[i];
    /* A routine port does not imply a fully integrated event. Support remains
       zero until the board input/task binding is complete for that producer. */
    ecu.diagnostics.mil_flashing_requested =
        (u8)(oem_runtime.state.events.store.demand == 2U);
    if (oem_runtime.initialized && !oem_runtime.stopped && !oem_runtime.fault)
        ecu.diagnostics.mil_steady = oem_runtime.state.mil.lamp;
    else
        /* Standalone fallback for retained demand: never clear a remembered
           warning because its runtime producer is unavailable. Not OEM parity. */
        ecu.diagnostics.mil_steady = (u8)(ecu.key_input &&
            (oem_runtime.state.events.store.demand || (oem_runtime.state.mil.retained & 1U)));
    diagnostic_mil_update(now);
}
void oem_runtime_boot(void) {
    OemRuntime *r = &oem_runtime;
    u8 restored;
    if (r->booted || ecu.iac.off_pending)
        return;
    restored = oem_history_owner_load(&r->history, &r->state);
    if (!r->history.ready) {
        r->fault = 1;
        return;
    }
    r->state.events.startup_flags = restored ? 0U : 0x8000U;
    if (!oem_dtc_cycle_begin(&r->state.events)) {
        r->fault = 1;
        return;
    }
    oem_dtc_drive_init(&r->state.events);
    oem_dtc_warmup_init(&r->state.events);
    /* These bits describe this ignition cycle, not retained completion. */
    r->state.events.gate &= 0xFF3FU;
    r->state.events.store.phases[4] = r->state.events.store.phases[5] = 0;
    oem_diagnostics_bind(&r->state);
    oem_cadence_init(&r->cadence);
    r->cadence.enabled = 31U;
    r->booted = 1;
    r->fault = 0;
    oem_runtime_changed();
    publish(ecu.milliseconds);
}

static void initialize_inputs(void) {
    OemDiagnostics *s = &oem_runtime.state;
    diagnostic_monitors_init(&oem_runtime.monitors);
    oem_readiness_init(&s->readiness);
    oem_mil_on(&s->mil);
    oem_runtime.initialized = 1;
    oem_runtime_changed();
}

static void standalone_context(OemDtcState *s) {
    s->context[0] = (u8)clamp32(ecu.sensors.clt.value + 40, 0, 255);
    s->context[1] = (u8)clamp32(ecu.sensors.iat.value + 40, 0, 255);
    s->context[2] = (u8)clamp32(ecu.sensors.tps.value / 4, 0, 255);
    s->context[3] = (u8)clamp32(ecu.sensors.map.value, 0, 255);
    s->context[4] = (u8)clamp32(ecu.sensors.battery.value / 100, 0, 255);
    s->context[5] = (u8)clamp32(ecu.rotation.rpm / 32U, 0, 255);
    s->context[6] = (u8)clamp32(ecu.sensors.speed_kph, 0, 255);
    s->context[7] = ecu.control.mode;
    /* Fuel trim in 1/128 (128 = neutral), as in monitor byte 19. The raw
       1/1024 value always saturated this byte. */
    s->context[8] = (u8)(ecu.control.applied_trim / 8U > 255U ? 255U : ecu.control.applied_trim / 8U);
    s->coolant = s->context[0];
    s->run_flags = ecu.control.mode == ENGINE_RUNNING ? 4U :
                   (ecu.control.mode == ENGINE_CRANKING ? 2U : 0U);
}

static u8 standalone_cycles(u32 now) {
    OemRuntime *r = &oem_runtime;
    OemDtcState *s = &r->state.events;
    u8 i, completed[14];
    if (ecu.key_input && ecu.control.mode == ENGINE_RUNNING && ecu.rotation.state == ROT_VALID) {
        if (!r->drive_timing) {
            r->drive_since = now;
            r->drive_timing = 1;
        }
        /* Explicit standalone drive qualification, once per ignition cycle;
           this is not an assertion about the OEM task's time unit. */
        if (!r->drive_qualified && now - r->drive_since >= 3000UL) {
            r->drive_qualified = 1;
            if (s->drive_count < 65535U) s->drive_count++;
        }
        if (ecu.sensors.clt.quality == QUALITY_VALID) {
            if (!r->warmup_seeded) {
                r->warmup_start_c = ecu.sensors.clt.value;
                r->warmup_seeded = 1;
            }
            /* Supplied Bosch PDF p53/technical p47: rise >=22 C and >=70 C.
               Use engineering units here, never native coolant-byte limits. */
            if (!r->warmup_qualified && ecu.sensors.clt.value >= 70 &&
                (s32)ecu.sensors.clt.value - r->warmup_start_c >= 22L) {
                r->warmup_qualified = 1;
                if (s->warmup_count < 255U) s->warmup_count++;
            }
        }
    } else
        r->drive_timing = 0;
    /* Keep native phase bytes clear while producers ingest. Otherwise a
       recover/reassert pair can consume several drive counts in one ignition.
       A monitor completing after qualification must still get its one callback. */
    if (r->drive_qualified) {
        for (i = 0; i < 14U; i++) {
            completed[i] = r->monitors.completed[i] & (u8)~r->drive_completed[i];
            r->drive_completed[i] |= completed[i];
        }
        if (!oem_dtc_phase_masked(&s->store, 0x9519U, completed)) return 0;
    }
    if (r->warmup_qualified) {
        for (i = 0; i < 14U; i++) {
            completed[i] = r->monitors.completed[i] & (u8)~r->warmup_completed[i];
            r->warmup_completed[i] |= completed[i];
        }
        if (!oem_dtc_phase_masked(&s->store, 0x952BU, completed)) return 0;
    }
    if (r->monitors.misfire_qualified) {
        /* One completed 128-window observation advances applicable misfire
           confirmation/healing once. Unknown cylinder identity never heals or
           confirms a cylinder record from an old completed descriptor. */
        s->store.phases[4] = s->store.phases[5] = 1;
        if (!oem_dtc_phase_masked(&s->store, 0x9593U, r->monitors.completed) ||
            !oem_dtc_phase_masked(&s->store, 0x9594U, r->monitors.completed)) return 0;
        s->store.phases[4] = s->store.phases[5] = 0;
    }
    return 1;
}

void oem_runtime_poll(u32 now, u32 ticks) {
    OemRuntime *r = &oem_runtime;
    OemDiagnostics *s = &r->state;
    oem_history_owner_poll(&r->history, now);
    if (!r->booted) {
        publish(now);
        return;
    }
    if (r->stopped || r->fault || !ecu.cal.valid) {
        publish(now);
        return;
    }
    if (!r->initialized)
        initialize_inputs();
    if (!r->clock_seen) {
        r->last_ticks = ticks;
        r->clock_seen = 1;
        publish(now);
        return;
    }
    if (ticks - r->last_ticks >= 0x80000000UL) {
        r->fault = 2;
        publish(now);
        return;
    }
    r->last_ticks = ticks;
    /* Between 10 ms monitor releases nothing published has changed; copying
       the 107 live words on every foreground pass cost ~6k cycles each. The
       MIL flash phase keeps 10 ms resolution. */
    if (now - r->monitors.last_update < 10UL)
        return;
    standalone_context(&s->events);
    diagnostic_monitors_update(&r->monitors, &s->events, now, cal_active());
    if (!standalone_cycles(now))
        r->fault = 3;
    if (now - r->cycle_at >= 100UL) {
        r->cycle_at = now;
        oem_dtc_clock(&s->events);
        if (!oem_dtc_aggregate(&s->events.store))
            r->fault = 3;
        s->mil.demand = s->events.store.demand;
        s->mil.fd08 = ecu.key_input ? 8192U : 0U;
        s->mil.fd6a = ecu.rotation.state == ROT_VALID ? 64U : 0U;
        oem_mil_update(&s->mil);
        /* Standalone lamp release: after prove-out, a fully healed/cleared
           record demand releases the native steady-state latch. */
        if (!s->mil.demand && (s->mil.prove_flags & 1U))
            oem_mil_off(&s->mil);
        s->events.gate = (u16)((s->events.gate & 0xFEFFU) | (s->mil.lamp ? 256U : 0U));
    }
    /* Each release performs at most one maintenance step. */
    if (!oem_dtc_maintain(&s->events))
        r->fault = 3;
    if (s->events.clear_request) {
        if (!oem_diagnostics_clear_ported(s))
            r->fault = 5;
        else {
            diagnostic_monitors_init(&r->monitors);
            r->drive_timing = r->warmup_seeded = 0;
        }
    }
    oem_runtime_changed();
    publish(now);
}
void oem_runtime_stop(void) {
    OemRuntime *r = &oem_runtime;
    u16 lock;
    u32 now;
    if (r->stopped)
        return;
    r->stopped = 1;
    if (r->initialized && !r->fault) {
        r->state.events.store.phases[2] = r->drive_qualified;
        r->state.events.store.phases[3] = r->warmup_qualified;
        oem_dtc_age(&r->state.events);
        oem_runtime_changed();
    }
    lock = hal_lock();
    now = ecu.milliseconds;
    hal_unlock(lock);
    publish(now);
}
u8 oem_runtime_save(u32 now) {
    if (!oem_runtime.booted || !oem_runtime.stopped)
        return 0;
    return oem_history_owner_save(&oem_runtime.history, &oem_runtime.state, now);
}
u8 oem_runtime_settled(void) {
    return (u8)(oem_runtime.booted && oem_runtime.stopped &&
        !oem_runtime.state.events.clear_request &&
        oem_history_owner_settled(&oem_runtime.history));
}
