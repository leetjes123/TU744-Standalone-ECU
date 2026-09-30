#include "control.h"
#include "lifecycle.h"
#include "oem_runtime.h"
#include <assert.h>
#include <string.h>
extern u8 fake_eeprom[8192], fake_eeprom_status, fake_eeprom_read_fail;
extern u8 fake_run_permission, fake_power_release_allowed, fake_iac_ok, fake_iac_response;
extern u8 fake_hold_fault;
extern s32 fake_write_budget;
extern u32 fake_system_resets;
static unsigned checks;
#define VERIFY(x) do { checks++; assert(x); } while (0)

static void prepare(void) {
    ecu_init(1);
    cal_example(ecu.cal.bytes[0]);
    ecu.cal.valid = 1;
    ecu.cal.generation = 1;
    ecu.key_input = 1;
    fake_iac_ok = 1;
    fake_iac_response = 0xC0;
    fake_hold_fault = fake_eeprom_status = fake_eeprom_read_fail = 0;
    fake_write_budget = -1;
    fake_run_permission = 1;
    fake_power_release_allowed = 0;
    fake_system_resets = 0;
}
static void oxygen(u32 now, u16 raw) {
    ecu.milliseconds = now;
    adc_publish((u16)(0x6000U | raw), now);
    sensors_update(now);
}
static void analog_policy(void) {
    u16 error, i;
    u8 *c;
    prepare();
    c = ecu.cal.bytes[0];
    c[0x600] = 1;
    c[CAL_FLAGS] |= EQUIP_UPSTREAM_RELAY_HEATER;
    ecu.wideband_input = 1; /* A fictitious ready wire cannot bypass policy. */
    oxygen(1000, 512);
    /* Policy 0: no warm-up, usable as soon as the sample is valid. */
    VERIFY(ecu.sensors.wideband_ready && wideband.state == WB_DIRECT);
    c[CAL_FLAGS] = (u8)(c[CAL_FLAGS] & 0xFEU);
    oxygen(1010, 512);
    VERIFY(!ecu.sensors.wideband_ready && wideband.state == WB_DISABLED);
    c[CAL_FLAGS] |= EQUIP_UPSTREAM_RELAY_HEATER;
    c[CAL_WB_POLICY] = 1;
    VERIFY(!cal_validate(c, &error));
    put16(c + CAL_WB_WARM_MS, 10000);
    put16(c + CAL_WB_GOOD_MS, 500);
    put16(c + CAL_WB_MIN_MV, 100);
    put16(c + CAL_WB_MAX_MV, 4900);
    VERIFY(cal_validate(c, &error));
    oxygen(0xFFFFF000UL, 512);
    VERIFY(wideband.state == WB_WARMING);
    for (i = 1; i <= 1050U; i++)
        oxygen(0xFFFFF000UL + (u32)i * 10UL, 512);
    VERIFY(ecu.sensors.wideband_ready && wideband.state == WB_ANALOG_QUALIFIED);
    oxygen(0xFFFFF000UL + 11500UL, 512); /* missed service breaks qualification */
    VERIFY(!ecu.sensors.wideband_ready && wideband.state == WB_UNAVAILABLE);
    oxygen(0xFFFFF000UL + 11510UL, 0);
    VERIFY(!ecu.sensors.wideband_ready && wideband.state == WB_UNAVAILABLE);
    oxygen(0xFFFFF000UL + 11520UL, 512);
    VERIFY(!ecu.sensors.wideband_ready && wideband.state == WB_WARMING);
    oxygen(0xFFFFF000UL + 12520UL, 512);
    VERIFY(!ecu.sensors.wideband_ready && wideband.state == WB_WARMING);
    ecu.key_input = 0;
    oxygen(8000, 512);
    VERIFY(wideband.state == WB_DISABLED);
    ecu.key_input = 1;
    oxygen(8010, 512);
    VERIFY(wideband.state == WB_WARMING);
}
static void launch_and_gear(void) {
    u8 *c;
    u16 error;
    Rotation r;
    EnginePlan p;
    prepare();
    memset(&r, 0, sizeof(r));
    memset(&p, 0, sizeof(p));
    c = ecu.cal.bytes[0];
    c[0x5D4] |= CFG_LAUNCH;
    c[0x7B4] = 8;
    put16(c + CAL_LAUNCH_MS, 5000);
    put16(c + 0x5E7, 4000);
    put16(c + CAL_LAUNCH_SOFT_RPM, 3000);
    c[CAL_LAUNCH_SOFT_PERCENT] = 80;
    c[0x7AF] = 10;
    c[0x8C5] = 50;
    ecu.authority.inhibits = 0;
    ecu.sensors.tps.quality = QUALITY_VALID;
    ecu.sensors.tps.value = 0;
    ecu.control.mode = ENGINE_RUNNING;
    r.state = ROT_VALID;
    r.rpm = 3500;
    VERIFY(cal_validate(c, &error));
    VERIFY(!launch_arm(0, 2000)); /* Missing VSS is not zero speed. */
    VERIFY(!launch_permitted(2000));
    VERIFY(launch_arm(1, 2000));
    ecu.sensors.tps.value = 600;
    limits_update(2010, &r, c, &p);
    VERIFY(ecu.control.launch && p.soft_fuel == 40U && p.soft_spark == 40U);
    VERIFY(!p.fuel_cut && !p.spark_cut);
    r.rpm = 4000;
    limits_update(2020, &r, c, &p);
    VERIFY((p.fuel_cut & CUT_LAUNCH) && (p.spark_cut & CUT_LAUNCH));
    ecu.vss_count++;
    limits_update(2030, &r, c, &p);
    VERIFY(!vehicle.armed && !ecu.control.launch && !(p.fuel_cut & CUT_LAUNCH));
    ecu.sensors.tps.value = 0;
    VERIFY(launch_arm(1, 3000));
    VERIFY(!launch_permitted(8000));
    VERIFY(launch_arm(1, 0xFFFFFF00UL));
    VERIFY(launch_permitted(0x00000010UL));
    ecu.cal.generation++;
    VERIFY(!launch_permitted(0x00000020UL));

    c[0x7B4] = 0xC8;
    c[0x7B8] = 30; /* -5 degrees in schema3 ignition coordinates */
    c[0x7B9] = 80;
    put16(c + CAL_ANTILAG_MS, 1000);
    c[CAL_ANTILAG_CLT] = 105;
    c[CAL_ANTILAG_IAT] = 60;
    VERIFY(cal_validate(c, &error));
    ecu.sensors.clt.quality = ecu.sensors.iat.quality = QUALITY_VALID;
    ecu.sensors.clt.value = 80;
    ecu.sensors.iat.value = 20;
    VERIFY(launch_arm(1, 3000));
    ecu.sensors.tps.value = 600;
    r.rpm = 3500;
    limits_update(3010, &r, c, &p);
    VERIFY(vehicle.antilag && p.soft_fuel == 0 && p.soft_spark == 40);
    r.rpm = 4000;
    limits_update(3020, &r, c, &p);
    VERIFY(vehicle.antilag && !(p.fuel_cut & CUT_LAUNCH) && (p.spark_cut & CUT_LAUNCH));
    limits_update(4010, &r, c, &p);
    VERIFY(!vehicle.antilag && vehicle.antilag_used);
    limits_update(4020, &r, c, &p);
    VERIFY(!vehicle.antilag && (p.fuel_cut & CUT_LAUNCH));
    ecu.sensors.vss_valid = 1;
    ecu.sensors.speed_kph = 50;
    put16(c + 0x7A1, 400);
    put16(c + 0x7AD, 2000);
    put16(c + 0x7A3, 300);
    put16(c + 0x7A5, 200);
    put16(c + 0x7A7, 150);
    put16(c + 0x7A9, 100);
    put16(c + 0x7AB, 80);
    r.rpm = 2502;
    gear_update(&r);
    VERIFY(vehicle.gear == 3);
    ecu.sensors.vss_valid = 0;
    gear_update(&r);
    VERIFY(!vehicle.gear);
}
static void shutdown_history(void) {
    u32 now;
    prepare();
    memset(fake_eeprom, 255, sizeof(fake_eeprom));
    ecu.key_input = 0;
    oem_runtime_boot();
    VERIFY(oem_runtime.booted && oem_runtime.history.dirty);
    power_poll(0);
    power_poll(10);
    VERIFY(!ecu.key_input);
    power_poll(20);
    VERIFY(ecu.key_input && power.state == POWER_RUN);
    /* Standalone producers initialize without fabricated native-RAM bindings;
       the first release only seeds the independent capture clock. */
    oem_runtime_poll(20, 25000);
    VERIFY(oem_runtime.initialized && !ecu.diagnostics.support[0x61]);
    oem_runtime.state.events.timestamp = 123;
    oem_runtime.state.mil.retained = 1;
    oem_runtime_changed();
    fake_run_permission = 0;
    power_poll(30);
    power_poll(40);
    power_poll(50);
    VERIFY(!ecu.key_input && ecu.service && oem_runtime.stopped);
    VERIFY(power.state == POWER_SAVE && oem_runtime.history.journal.phase);
    for (now = 51; now < 1000; now++) {
        ecu.milliseconds = now;
        oem_runtime_poll(now, now * 1250UL);
        power_poll(now);
    }
    VERIFY(oem_runtime_settled() && power.state == POWER_HELD);
    ecu.sensors.clt.quality = QUALITY_VALID;
    power_poll(16000);
    VERIFY(power.release_blocked && power.state == POWER_HELD);
    fake_power_release_allowed = 1;
    fake_run_permission = 2; /* A lost observation cannot release on a later unsampled pass. */
    power_poll(16010);
    power_poll(16011);
    VERIFY(power.state == POWER_HELD && power.release_blocked);
    fake_run_permission = 0;
    power_poll(16020);
    power_poll(16030);
    VERIFY(power.state == POWER_HELD);
    power_poll(16040);
    VERIFY(power.state == POWER_RELEASED);
    prepare();
    oem_runtime_boot();
    VERIFY(oem_runtime.booted && oem_runtime.state.events.timestamp == 123);
    VERIFY(oem_runtime.state.mil.retained == 1);
    VERIFY(ecu.diagnostics.mil_steady);
    power_poll(0);
    power_poll(10);
    power_poll(20);
    fake_run_permission = 0;
    power_poll(30);
    power_poll(40);
    power_poll(50);
    VERIFY(oem_runtime.history.journal.phase && !fake_system_resets);
    fake_run_permission = 1;
    power_poll(60);
    power_poll(70);
    power_poll(80);
    VERIFY(power.state == POWER_RESTART_REQUIRED && !fake_system_resets);
    for (now = 81; now < 1000; now++) {
        ecu.milliseconds = now;
        oem_runtime_poll(now, now * 1250UL);
        power_poll(now);
    }
    VERIFY(power.state == POWER_RESET_REQUESTED && fake_system_resets == 1);
    power_poll(1010);
    VERIFY(fake_system_resets == 1 && !ecu.key_input);
    /* A failed load cannot authorize writing an empty replacement history. */
    prepare();
    fake_eeprom_read_fail = 1;
    oem_runtime_boot();
    VERIFY(!oem_runtime.booted && !oem_runtime.history.ready && oem_runtime.fault);
    fake_eeprom_read_fail = 0;
}
static void rotating_reassertion(void) {
    prepare();
    ecu.rotation.state = ROT_VALID; ecu.rotation.rpm = 3000;
    ecu.control.mode = ENGINE_RUNNING;
    power_poll(0); power_poll(10); power_poll(20);
    VERIFY(power.state == POWER_RUN);
    fake_run_permission = 0;
    power_poll(30); power_poll(40); power_poll(50);
    VERIFY(power.state == POWER_DRAIN && (ecu.authority.inhibits & INH_POWER));
    fake_run_permission = 1;
    fake_eeprom_status = 1;
    power_poll(60); power_poll(70); power_poll(80);
    VERIFY(power.state == POWER_RESTART_REQUIRED && !fake_system_resets);
    fake_eeprom_status = 0;
    ecu.storage_owner = &power;
    power_poll(81); VERIFY(!fake_system_resets);
    ecu.storage_owner = 0;
    power_poll(82);
    VERIFY(power.state == POWER_RESET_REQUESTED && fake_system_resets == 1);
    VERIFY(ecu.rotation.state == ROT_VALID && ecu.rotation.rpm == 3000);
    power_poll(90); VERIFY(fake_system_resets == 1);
}
unsigned test_lifecycle(void) {
    checks = 0;
    analog_policy();
    launch_and_gear();
    shutdown_history();
    rotating_reassertion();
    return checks;
}
