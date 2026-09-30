/* Correct-behavior regressions for the 22 September production audit.
 * The original audit counterexamples are immutable historical evidence. */
#include "control.h"
#include "lifecycle.h"
#include "oem_runtime.h"
#include "faults.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern u8 fake_eeprom[8192], fake_run_permission, fake_heaters;
extern u8 fake_eeprom_status, fake_eeprom_read_fail, fake_write_fail, fake_iac_ok;
extern s32 fake_write_budget;
extern u16 fake_irq;
extern u32 fake_system_resets;
static DiagnosticMonitors m;
static OemDtcState events;
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { printf("AUDIT FAIL line %d: %s\n", __LINE__, #x); assert(x); } } while (0)

static void setup_cycle(u8 erase) {
    fake_eeprom_status = fake_eeprom_read_fail = fake_write_fail = 0;
    fake_write_budget = -1;
    fake_iac_ok = fake_run_permission = 1;
    fake_irq = 1;
    fake_system_resets = 0;
    if (erase) memset(fake_eeprom, 255, sizeof(fake_eeprom));
    ecu_init(1);
    cal_example(ecu.cal.bytes[0]);
    ecu.cal.valid = 1;
    ecu.cal.generation = 1;
    ecu.key_input = ecu.control.key_on = 1;
    ecu.control.mode = ENGINE_RUNNING;
    ecu.rotation.state = ROT_VALID;
    ecu.rotation.epoch = ecu.rotation.seen = 1;
    ecu.rotation.rpm = 3000;
    ecu.authority.inhibits = 0;
    ecu.authority.plan.epoch = ecu.authority.epoch;
    ecu.authority.plan.max_age_ms = 50;
    ecu.authority.plan.pulse_us = 1000;
    ecu.sensors.tps.quality = ecu.sensors.map.quality = QUALITY_VALID;
    ecu.sensors.clt.quality = ecu.sensors.iat.quality = QUALITY_VALID;
    ecu.sensors.battery.quality = ecu.sensors.oxygen.quality = QUALITY_VALID;
    ecu.sensors.battery.value = 14000;
    ecu.sensors.clt.value = 40;
    memset(&events, 0, sizeof(events));
    events.startup_flags = 0x8000;
    CHECK(oem_dtc_cycle_begin(&events));
    diagnostic_monitors_init(&m);
}

static void setup(void) { setup_cycle(1); }

static void update(u32 now) {
    ecu.milliseconds = now;
    diagnostic_monitors_update(&m, &events, now, cal_active());
}

static void push(u32 duration, u8 slot, u8 phase, u32 now) {
    u8 head = misfire_observation.head;
    misfire_observation.duration[head] = duration;
    misfire_observation.epoch[head] = ecu.rotation.epoch;
    misfire_observation.slot[head] = slot;
    misfire_observation.phase[head] = phase;
    misfire_observation.eligible[head] = 1;
    misfire_observation.at_ms[head] = now;
    misfire_observation.head = (u8)((head + 1U) & 7U);
}

static u8 *record(OemDtcState *s, u8 event) {
    u8 i;
    for (i = 0; i < s->store.count; i++)
        if (s->store.records[i][0] == event) return s->store.records[i];
    return 0;
}

static void test_aux_and_mil(void) {
    u16 i;
    static const u16 reasons[] = {INH_OUTPUT, INH_DEADLINE, INH_POWER, INH_CAL, INH_SERVICE, INH_BOARD};
    setup();
    ecu.cal.bytes[0][0x600] = 1;
    ecu.cal.bytes[0][CAL_FLAGS] |= EQUIP_UPSTREAM_RELAY_HEATER;
    auxiliary_update(0, &ecu.rotation, cal_active()); CHECK(fake_heaters == 1);
    for (i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++) {
        ecu.authority.inhibits = reasons[i];
        auxiliary_update(0, &ecu.rotation, cal_active()); CHECK(!fake_heaters);
    }
    ecu.authority.inhibits = INH_SYNC; /* key-on controller power remains allowed */
    auxiliary_update(0, &ecu.rotation, cal_active()); CHECK(fake_heaters == 1);
    ecu.diagnostics.mil_steady = 0;
    ecu.diagnostics.mil_flashing_requested = 1;
    diagnostic_mil_update(0xFFFFFF00UL); CHECK(ecu.diagnostics.mil_output);
    diagnostic_mil_update(243UL); CHECK(ecu.diagnostics.mil_output); /* +499, wrap */
    diagnostic_mil_update(244UL); CHECK(!ecu.diagnostics.mil_output);
    diagnostic_mil_update(744UL); CHECK(ecu.diagnostics.mil_output);
    ecu.diagnostics.mil_flashing_requested = 0;
    ecu.diagnostics.mil_steady = 1;
    diagnostic_mil_update(745); CHECK(ecu.diagnostics.mil_output);
    ecu.diagnostics.mil_steady = 0;
    diagnostic_mil_update(746); CHECK(!ecu.diagnostics.mil_output);
    ecu.diagnostics.mil_flashing_requested = 1;
    diagnostic_mil_update(747); CHECK(ecu.diagnostics.mil_output);
    ecu.key_input = 0;
    diagnostic_mil_update(748); CHECK(!ecu.diagnostics.mil_output);
}

static void test_phase_and_cuts(void) {
    u16 i, pct, fuel[2], spark[2];
    setup();
    diagnostic_phase_arm(1000, 1, 0); diagnostic_phase_capture(66636UL); update(10);
    diagnostic_phase_arm(100000UL, 1, 0); diagnostic_phase_capture(165666UL); update(20);
    CHECK(!m.phase_valid && !m.phase_seeded);
    diagnostic_phase_arm(0xFFFFFFF0UL, 1, 0); diagnostic_phase_capture(84); update(30);
    CHECK(m.phase_last_delay == 100 && !m.phase_valid);
    diagnostic_phase_arm(1000, 1, 0); diagnostic_phase_capture(1130); update(40);
    CHECK(m.phase_valid);
    ecu.rotation.epoch++;
    update(50); CHECK(!m.phase_valid && !misfire_observation.phase_identity);
    setup();
    ecu.authority.plan.soft_spark = 100;
    CHECK(!coil_admit(0, ecu.authority.epoch, 3000));
    coil_done(0); CHECK(!phase_observation.armed);
    ecu.authority.plan.soft_spark = 0;
    CHECK(coil_admit(0, ecu.authority.epoch, 3000));
    coil_done(0); CHECK(phase_observation.armed);
    phase_observation.armed = 0;
    coil_done(0); CHECK(!phase_observation.armed); /* duplicate completion */
    for (pct = 0; pct <= 100; pct++) {
        setup();
        fuel[0] = fuel[1] = spark[0] = spark[1] = 0;
        ecu.authority.plan.soft_fuel = ecu.authority.plan.soft_spark = (u8)pct;
        for (i = 0; i < 200; i++) {
            fuel[i & 1] += injector_admit((u8)(i & 1));
            injector_done(0); injector_done(1); injector_done(2); injector_done(3);
            spark[i & 1] += coil_admit((u8)(i & 1), ecu.authority.epoch, 3000);
            coil_done((u8)(i & 1));
        }
        CHECK(fuel[0] == 100U - pct && fuel[1] == fuel[0]);
        CHECK(spark[0] == fuel[0] && spark[1] == spark[0]);
    }
}

static void test_transaction_and_temperature(void) {
    u16 i;
    static const s16 temperature[] = {-40, 0, 127, 128, 140, 150};
    setup();
    CHECK(cal_begin());
    put16(ecu.cal.bytes[1] + 0x460, (u16)-29);
    CHECK(!cal_commit(0) && ecu.cal.active == 0);
    cal_abort();
    ecu.milliseconds = 0xFFFFFF00UL;
    CHECK(cal_begin());
    cal_poll(4743); CHECK(ecu.cal.staging); /* 4999 elapsed */
    cal_poll(4744); CHECK(!ecu.cal.staging && ecu.cal.valid);
    setup();
    oem_runtime_boot();
    CHECK(cal_begin());
    ecu.control.mode = ENGINE_STOPPED;
    ecu.rotation.state = ROT_UNSYNCED;
    ecu.rotation.rpm = ecu.rotation.seen = 0;
    fake_run_permission = 0;
    for (i = 0; i < 1000; i++) {
        if (i == 10) fake_run_permission = 1;
        ecu.milliseconds = (u32)i * 10UL;
        power_poll(ecu.milliseconds);
        oem_runtime_poll(ecu.milliseconds, ecu.milliseconds * 1250UL);
    }
    CHECK(!ecu.cal.staging && fake_system_resets == 1);
    for (i = 0; i < sizeof(temperature) / sizeof(temperature[0]); i++) {
        setup();
        ecu.sensors.clt.value = ecu.sensors.iat.value = temperature[i];
        protocol_receive(0xAA); protocol_receive(1); protocol_receive(0x10); protocol_receive(0x11);
        protocol_poll(1);
        CHECK(ecu.protocol.tx_length == 101 && ecu.protocol.tx[82] == 4);
        /* Extension 3 retains the inhibit word at payload 89 (tx +2). */
        CHECK(get16(ecu.protocol.tx + 91) == ecu.authority.inhibits);
        CHECK((s16)get16(ecu.protocol.tx + 84) == temperature[i]);
        CHECK((s16)get16(ecu.protocol.tx + 86) == temperature[i]);
        CHECK((s8)ecu.protocol.tx[14] == (temperature[i] > 127 ? 127 : temperature[i]));
    }
}

static void test_rotation_and_speed_faults(void) {
    u16 i;
    setup();
    ecu.sensors.speed_kph = 60; ecu.sensors.vss_valid = 1; ecu.vss_edge_stamp = 1000;
    update(1000);
    ecu.sensors.speed_kph = ecu.sensors.vss_valid = 0;
    for (i = 1500; i < 4000; i += 10) update(i);
    CHECK(events.live[0x68] & 1);
    ecu.sensors.speed_kph = 5; ecu.sensors.vss_valid = 1;
    for (i = 4000; i < 4050; i += 10) update(i);
    CHECK(!(events.live[0x68] & 1));
    ecu.sensors.speed_kph = ecu.sensors.vss_valid = 0;
    for (i = 4500; i < 6500; i += 10) update(i);
    CHECK(!(events.live[0x68] & 1)); /* normal deceleration to stop */
    setup(); update(10);
    ecu.rotation.losses++; ecu.rotation.epoch++;
    ecu.rotation.state = ROT_UNSYNCED;
    ecu.rotation.seen = 0; ecu.control.mode = ENGINE_STOPPED;
    update(20); update(30); update(40);
    CHECK((events.live[0x0D] & 1) && (events.live[0x4A] & 1));
    setup(); ecu.rotation.losses = 65536UL;
    for (i = 10; i < 100; i += 10) update(i);
    CHECK(!(events.live[0x4A] & 1) && m.previous_losses == 65536UL);
    ecu.rotation.losses++;
    update(100); update(110); update(120);
    CHECK(events.live[0x4A] & 1); /* even if sync reacquired between polls */
}

static void test_misfire_and_runtime(void) {
    u16 i;
    u32 now = 0;
    u8 *r;
    setup();
    for (i = 0; i < 400; i++) {
        now = (u32)(i + 1U) * 10UL;
        push(i < 150 ? 10000UL : 20000UL, (u8)(i & 1), 0, now); update(now);
    }
    CHECK(!(events.live[3] & 1) && !(events.live[8] & 1));
    CHECK(m.misfire_baseline[0] == 20000UL && m.misfire_baseline[1] == 20000UL);
    for (i = 0; i < 400; i++) {
        now += 10;
        push(20000UL + (u32)i * 100UL, (u8)(i & 1), 0, now); update(now);
    }
    CHECK(!(events.live[3] & 1));
    ecu.authority.plan.soft_fuel = 50;
    push(99999UL, 0, 2, now); update(now + 10);
    CHECK(!m.misfire_seeded && misfire_observation.head == misfire_observation.tail);
    setup();
    diagnostic_rotation_edge(1, 1, 29, 0, 1);
    CHECK(!misfire_observation.seeded);
    diagnostic_rotation_edge(100, 1, 30, 0, 1);
    diagnostic_rotation_edge(10100, 1, 0, 1, 1);
    diagnostic_rotation_edge(20100, 1, 30, 0, 1);
    CHECK(misfire_observation.head == 2 && misfire_observation.duration[0] == 10000UL &&
          misfire_observation.duration[1] == 10000UL);
    update(10); CHECK(m.misfire_seeded == 2);
    push(10000, 1, 1, 0); update(300);
    CHECK(!m.misfire_seeded); /* stale queued sample */
    push(10000, 0, 1, 310); ecu.rotation.epoch++; update(310);
    CHECK(!m.misfire_seeded); /* old-epoch queued sample */
    push(10000, 1, 1, 320); update(320); CHECK(m.misfire_seeded == 1);
    misfire_observation.overflow = 1; update(330);
    CHECK(!m.misfire_seeded && misfire_observation.head == misfire_observation.tail);
    setup(); oem_runtime_boot(); oem_runtime_poll(0, 0);
    for (i = 1; i <= 520; i++) {
        now = (u32)i * 10UL;
        ecu.milliseconds = now;
        push((i & 3U) == 1U ? 15000UL : 10000UL, (u8)(i & 1U), 1, now);
        oem_runtime_poll(now, now * 1250UL);
    }
    r = record(&oem_runtime.state.events, 3);
    CHECK(r && r[6] == 0 && (r[4] & 1));
    CHECK(oem_runtime.state.events.run_flags == 4 && oem_runtime.state.events.drive_count == 1);
    CHECK(ecu.diagnostics.mil_steady && ecu.diagnostics.mil_output);
    ecu.sensors.clt.value = 70;
    for (i = 521; i < 1500; i++) {
        now = (u32)i * 10UL;
        ecu.milliseconds = now;
        push(10000UL, (u8)(i & 1U), 1, now);
        oem_runtime_poll(now, now * 1250UL);
    }
    r = record(&oem_runtime.state.events, 3);
    CHECK(r && !(r[4] & 1) && !(oem_runtime.state.events.live[3] & 1));
    CHECK(oem_runtime.state.events.warmup_count == 1);
    CHECK(!ecu.diagnostics.mil_steady);
    /* Missing phase can neither confirm nor heal an old cylinder record. */
    memset(m.completed, 0, sizeof(m.completed));
    r = record(&oem_runtime.state.events, 7);
    CHECK(r != 0);
    r[4] |= 1; r[2] = 2; r[7] = 2;
    CHECK(oem_dtc_phase_masked(&oem_runtime.state.events.store, 0x9594, m.completed));
    CHECK(r[7] == 2 && (r[4] & 1));
}

static void test_retained_drive_cycles(void) {
    u16 i;
    u8 cycle, *r;
    u32 now;
    for (cycle = 0; cycle < 7U; cycle++) {
        setup_cycle((u8)(cycle == 0));
        oem_runtime_boot(); oem_runtime_poll(0, 0);
        ecu.sensors.tps.quality = cycle < 3U ? QUALITY_RANGE : QUALITY_VALID;
        for (i = 1; i < 450U; i++) {
            now = (u32)i * 10UL; ecu.milliseconds = now;
            oem_runtime_poll(now, now * 1250UL);
        }
        r = record(&oem_runtime.state.events, 0x1B);
        CHECK(r != 0 && !oem_runtime.fault);
        if (cycle < 3U) CHECK(r[6] == 2U - cycle);
        CHECK(!!(r[4] & 1U) == (cycle >= 2U && cycle < 5U));
        CHECK(oem_runtime.state.events.drive_count == cycle + 1U);
        if (cycle == 0) {
            /* Bounce the verdict after qualification: still only one drive. */
            ecu.sensors.tps.quality = QUALITY_VALID;
            for (i = 450; i < 470; i++) {
                now = (u32)i * 10UL; ecu.milliseconds = now;
                oem_runtime_poll(now, now * 1250UL);
            }
            ecu.sensors.tps.quality = QUALITY_RANGE;
            for (i = 470; i < 490; i++) {
                now = (u32)i * 10UL; ecu.milliseconds = now;
                oem_runtime_poll(now, now * 1250UL);
            }
            CHECK(r[6] == 2U && !(r[4] & 1U));
        }
        ecu.rotation.state = ROT_UNSYNCED;
        ecu.rotation.rpm = ecu.rotation.seen = 0;
        ecu.control.mode = ENGINE_STOPPED;
        oem_runtime_stop();
        CHECK(oem_runtime_save(5000));
        for (i = 0; i < 1000U && !oem_runtime_settled(); i++)
            oem_runtime_poll(5000UL + (u32)i * 10UL, 6250000UL + (u32)i * 12500UL);
        CHECK(oem_runtime_settled());
    }
}

static u8 tool(u8 cmd) {
    while (ecu.protocol.tx_length) /* finish the previous reply first */
        protocol_poll(ecu.milliseconds);
    protocol_receive(0xAA); protocol_receive(1); protocol_receive(cmd);
    protocol_receive((u8)(cmd + 1U));
    protocol_poll(ecu.milliseconds);
    return ecu.protocol.tx[2];
}

/* Command 30 clears native records and inactive private records, only with
   the engine stopped; the monitor packet then reports zero stored DTCs. */
static void test_dtc_clear(void) {
    u16 i;
    u32 now = 0;
    u8 *r;
    setup(); oem_runtime_boot(); oem_runtime_poll(0, 0);
    for (i = 1; i <= 520; i++) {
        now = (u32)i * 10UL;
        ecu.milliseconds = now;
        push((i & 3U) == 1U ? 15000UL : 10000UL, (u8)(i & 1U), 1, now);
        oem_runtime_poll(now, now * 1250UL);
    }
    r = record(&oem_runtime.state.events, 3);
    CHECK(r && ecu.diagnostics.mil_steady);
    CHECK(r[14] == 3000U / 32U && r[19] == 128U); /* freeze frame: rpm, trim 1/128 */
    fault_set(FAULT_TPS, 2, now);
    fault_set(FAULT_TPS, 0, now);
    fault_set(FAULT_CLT, 2, now); /* still active: stays stored */
    CHECK(faults.stored == 6U);
    CHECK(tool(0x30) == 1); /* running: rejected */
    CHECK(record(&oem_runtime.state.events, 3) != 0);
    ecu.control.mode = ENGINE_STOPPED;
    ecu.rotation.state = ROT_UNSYNCED;
    ecu.rotation.rpm = 0;
    CHECK(tool(0x30) == 0);
    CHECK(faults.stored == 4U && faults.record[FAULT_TPS].occurrences == 0 && faults.dirty);
    CHECK(tool(0x30) == 1); /* a pending clear is not admitted twice */
    for (i = 521; i < 540; i++) {
        now = (u32)i * 10UL;
        ecu.milliseconds = now;
        oem_runtime_poll(now, now * 1250UL);
    }
    CHECK(!oem_runtime.fault && !oem_runtime.state.events.clear_request);
    CHECK(oem_runtime.state.events.store.count == 0 && !ecu.diagnostics.mil_steady);
    CHECK(oem_runtime.history.dirty);
    tool(0x10);
    CHECK(ecu.protocol.tx[2 + 91] == 0 && ecu.protocol.tx[2 + 92] == 0);
}

unsigned test_audit_regressions(void) {
    checks = 0;
    test_aux_and_mil();
    test_phase_and_cuts();
    test_transaction_and_temperature();
    test_rotation_and_speed_faults();
    test_misfire_and_runtime();
    test_retained_drive_cycles();
    test_dtc_clear();
    printf("PASS %u audit-fix regression assertions\n", checks);
    return checks;
}
