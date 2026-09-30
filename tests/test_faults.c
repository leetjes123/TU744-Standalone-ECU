#include "control.h"
#include "faults.h"
#include "lifecycle.h"
#include "oem_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern u8 fake_eeprom[8192], fake_eeprom_read_fail, fake_eeprom_status;
extern u8 fake_iac_ok, fake_iac_response, fake_hold_fault;
extern u8 fake_run_permission, fake_power_release_allowed;
extern s32 fake_write_budget;
extern u32 fake_writes;
static unsigned checks;
#define VERIFY(x) do { checks++; assert(x); } while (0)
static void prepare(void) {
    ecu_init(1);
    cal_example(ecu.cal.bytes[0]);
    ecu.cal.valid = 1;
    ecu.cal.generation = 1;
    ecu.key_input = 1;
    ecu.control.previous_key = 1;
    ecu.rotation.state = ROT_VALID;
    ecu.rotation.rpm = 1000;
    ecu.rotation.normal = 1250;
    ecu.rotation.epoch = 1;
    fake_iac_ok = 1;
    fake_iac_response = 0xC0;
    fake_hold_fault = 0;
    fake_eeprom_read_fail = fake_eeprom_status = 0;
    fake_write_budget = -1;
}
static void inputs(u32 now, int failed, u8 stale) {
    static const u8 channels[6] = {8, 10, 11, 5, 0, 6};
    static const u16 samples[6] = {400, 300, 400, 486, 300, 512};
    unsigned i;
    ecu.milliseconds = now;
    for (i = 0; i < 6; i++) {
        if (failed == (int)i && stale) continue;
        adc_publish((u16)((u16)channels[i] << 12) |
                    (failed == (int)i ? 1023U : samples[i]), now);
    }
}
static void pass(u32 now) {
    Rotation r;
    rotation_snapshot(&r);
    controls_update(now, &r);
}
static void sensor_failures(void) {
    unsigned i;
    Sensor *s;
    static const u8 ids[5] = {FAULT_TPS, FAULT_CLT, FAULT_IAT, FAULT_BATTERY, FAULT_MAP};
    static const s16 rail_values[5] = {1000, -30, -30, 28416, 300};
    for (i = 0; i < 5; i++) {
        prepare();
        s = i == 0 ? &ecu.sensors.tps : i == 1 ? &ecu.sensors.clt :
            i == 2 ? &ecu.sensors.iat : i == 3 ? &ecu.sensors.battery : &ecu.sensors.map;
        inputs(100, -1, 0); pass(100);
        VERIFY(!ecu.authority.inhibits && !faults.active);
        inputs(200, (int)i, 0); pass(200);
        VERIFY(s->quality == QUALITY_RANGE && s->value == rail_values[i]);
        VERIFY(faults.active == (1U << ids[i]));
        VERIFY(faults.record[ids[i]].occurrences == 1);
        inputs(1200, (int)i, 0); pass(1200);
        VERIFY(s->value == rail_values[i] && s->quality == QUALITY_RANGE);
        VERIFY(!ecu.authority.inhibits && ecu.authority.plan.pulse_us > 0);
        VERIFY(injector_admit(0));
        VERIFY(coil_admit(0, ecu.authority.epoch, ecu.authority.plan.dwell_us));
        coil_done(0); injector_done(0); injector_done(1);
        VERIFY(!ecu.service && faults.record[ids[i]].occurrences == 1);
        inputs(1210, -1, 0); pass(1210);
        VERIFY(s->quality == QUALITY_VALID && !faults.active);
        VERIFY(faults.stored == (1U << ids[i]));
        inputs(1400, (int)i, 1); pass(1400);
        VERIFY(s->quality == QUALITY_STALE);
        VERIFY(faults.record[ids[i]].occurrences == 2 && !ecu.authority.inhibits);
        /* No fault-transition flood simply because key-off stops acquisition. */
        ecu.key_input = 0;
        inputs(1410, (int)i, 0); pass(1410);
        VERIFY(faults.record[ids[i]].occurrences == 2);
    }
    prepare();
    ecu.milliseconds = 100;
    pass(100); /* No samples since boot: raw state is zero and remains invalid. */
    VERIFY(faults.active == 0x3EU && faults.stored == 0x3EU);
    VERIFY(ecu.sensors.map.value == 0 && ecu.sensors.tps.value == 0);
    /* Schema 4 also applies measured MAP while cranking: zero MAP computes
       zero fuel, with no sensor inhibit or replacement pressure. */
    VERIFY(!ecu.authority.inhibits && ecu.authority.plan.pulse_us == 0);
    VERIFY(!injector_admit(1));
    prepare();
    ecu.cal.bytes[0][0x5D4] |= CFG_ALPHA_N;
    inputs(100, 4, 0); pass(100);
    VERIFY(!(faults.active & (1U << FAULT_MAP)) && !ecu.authority.inhibits);
    /* A stale value comes from the last captured raw sample, even when the
       last filtered/valid value differs, and even across uptime rollover. */
    prepare();
    inputs(0xFFFFFF00UL, -1, 0); sensors_update(0xFFFFFF00UL);
    adc_publish(0x8064U, 0xFFFFFF10UL);
    ecu.milliseconds = 0xFFFFFF10UL; sensors_update(0xFFFFFF10UL);
    VERIFY(ecu.sensors.tps.value != 55);
    ecu.milliseconds = 200; sensors_update(200);
    VERIFY(ecu.sensors.tps.quality == QUALITY_STALE && ecu.sensors.tps.value == 55);
    ecu.milliseconds = 10000; sensors_update(10000);
    VERIFY(ecu.sensors.tps.value == 55); /* No timed substitute or last-good hold. */
    prepare();
    inputs(100, -1, 0); pass(100);
    adc_publish(0x500AU, 110); ecu.milliseconds = 110; pass(110);
    VERIFY(ecu.sensors.battery.quality == QUALITY_RANGE && ecu.sensors.battery.value == 277);
    VERIFY(!ecu.authority.inhibits);
}
static void calibration_faults(void) {
    u8 active;
    prepare();
    inputs(100, -1, 0); pass(100);
    active = ecu.cal.active;
    VERIFY(cal_begin());
    put16(ecu.cal.bytes[active ^ 1U] + 0x550, 0);
    VERIFY(!cal_commit(1));
    VERIFY(ecu.cal.active == active && ecu.cal.valid);
    VERIFY(faults.stored & 1U);
    VERIFY(faults.record[FAULT_CAL].reason == 2 && !ecu.authority.inhibits);
    VERIFY(injector_admit(0));
    cal_abort();
    prepare();
    ecu.cal.valid = 0;
    ecu.rotation.state = ROT_UNSYNCED;
    ecu.rotation.rpm = 0;
    ecu.milliseconds = 10;
    ecu_poll();
    VERIFY(ecu.authority.inhibits & INH_CAL);
    VERIFY(faults.active & 1U);
    VERIFY(!injector_admit(0) && !coil_admit(0, ecu.authority.epoch, 3000));
}
static void finish(u32 now) {
    unsigned i;
    for (i = 0; i < 100 && faults.phase; i++) faults_poll(now + i);
    VERIFY(!faults.phase);
}
static void stopped(void) {
    ecu.key_input = 0;
    ecu.rotation.state = ROT_UNSYNCED;
    ecu.rotation.rpm = 0;
    ecu.iac.off_pending = 0;
}
static void journal(void) {
    static u8 previous[8192];
    unsigned budget;
    prepare(); memset(fake_eeprom, 0xFF, sizeof(fake_eeprom));
    faults_load(); VERIFY(faults.loaded && !faults.stored);
    fault_set(FAULT_TPS, QUALITY_RANGE, 100);
    VERIFY(!faults_save(100) && !ecu.service && !ecu.storage_owner);
    stopped(); VERIFY(faults_save(100)); finish(101);
    VERIFY(faults_settled() && faults.result == 1 && !ecu.service);
    memcpy(previous, fake_eeprom, sizeof(previous));
    /* 1 invalidate + 96 payload + 1 commit: every prefix-torn write. */
    for (budget = 0; budget <= 98; budget++) {
        prepare(); memcpy(fake_eeprom, previous, sizeof(previous));
        faults_load(); VERIFY(faults.stored == 2 && !faults.active);
        fault_set(FAULT_CLT, QUALITY_STALE, 200);
        stopped(); fake_write_budget = (s32)budget;
        VERIFY(faults_save(200)); finish(201);
        VERIFY(!ecu.service && !ecu.storage_owner);
        VERIFY(!memcmp(fake_eeprom, previous, FAULT_SLOT1));
        VERIFY(!memcmp(fake_eeprom + FAULT_SLOT1 + FAULT_BYTES, previous + FAULT_SLOT1 + FAULT_BYTES,
                       sizeof(previous) - FAULT_SLOT1 - FAULT_BYTES));
        prepare(); faults_load();
        VERIFY(faults.stored == (budget == 98 ? 6U : 2U));
        VERIFY(faults.record[FAULT_TPS].occurrences == 1);
    }
    prepare(); memcpy(fake_eeprom, previous, sizeof(previous)); faults_load();
    fault_set(FAULT_CLT, QUALITY_STALE, 300); stopped(); VERIFY(faults_save(300));
    fault_set(FAULT_IAT, QUALITY_RANGE, 301); finish(302);
    VERIFY(faults.dirty && faults.changed);
    VERIFY(faults_save(500)); finish(501);
    VERIFY(faults_settled());
    prepare(); faults_load(); VERIFY(faults.stored == 14 && !faults.active);
    /* An unreadable journal never overwrites unknown retained history. */
    prepare(); fake_eeprom_read_fail = 1; faults_load();
    VERIFY(!faults.loaded && faults.result == 3);
    fault_set(FAULT_TPS, QUALITY_RANGE, 0); stopped();
    VERIFY(!faults_save(0));
    fake_eeprom_read_fail = 0;
}
static void shutdown_faults(void) {
    u32 now;
    prepare(); memset(fake_eeprom, 0xFF, sizeof(fake_eeprom));
    stopped(); faults_load(); oem_runtime_boot();
    fake_run_permission = 1; fake_power_release_allowed = 0;
    power_poll(0); power_poll(10); power_poll(20);
    VERIFY(power.state == POWER_RUN && ecu.key_input);
    fault_set(FAULT_TPS, QUALITY_RANGE, 25);
    fake_run_permission = 0;
    power_poll(30); power_poll(40); power_poll(50);
    VERIFY(faults.phase && !ecu.service && ecu.storage_owner == &faults);
    for (now = 51; now < 1000; now++) {
        ecu.milliseconds = now;
        power_poll(now);
        oem_runtime_poll(now, now * 1250UL);
    }
    VERIFY(faults_settled() && oem_runtime_settled() && power.state == POWER_HELD);
    VERIFY(!ecu.storage_owner && faults.result == 1);
    prepare(); faults_load(); oem_runtime_boot();
    VERIFY(faults.stored == 2 && faults.record[FAULT_TPS].occurrences == 1);
    VERIFY(oem_runtime.booted && !faults.active);
    fake_run_permission = 1;
}
unsigned test_faults(void) {
    checks = 0;
    sensor_failures(); calibration_faults(); journal(); shutdown_faults();
    printf("PASS %u non-blocking sensor/fault journal assertions\n", checks);
    return checks;
}
