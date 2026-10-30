/* Firmware 0.0.2: real controllers, calibrated boundaries and retained state. */
static EnginePlan idle_dfco_pass(u32 now, u16 rpm) {
    EnginePlan p;
    ecu.rotation.rpm = rpm;
    memset(&p, 0, sizeof(p));
    engine_state_update(now, &ecu.rotation, cal_active());
    limits_update(now, &ecu.rotation, cal_active(), &p);
    idle_update(now, 10, &ecu.rotation, cal_active());
    return p;
}
static void idle_dfco_setup(void) {
    setup();
    ecu.rotation.state = ROT_VALID;
    ecu.control.key_on = 1;
    ecu.control.mode = ENGINE_RUNNING;
    ecu.control.idle_position = 60;
    ecu.iac.state = IAC_READY;
    ecu.sensors.tps.quality = ecu.sensors.clt.quality = QUALITY_VALID;
    ecu.sensors.clt.value = 90;
    ecu.sensors.tps.value = 30; /* above the former hard-coded 2% */
    ecu.cal.bytes[0][0x5E6] = 3;
    ecu.cal.bytes[0][0x5EE] = 0;
    ecu.cal.bytes[0][0x5D4] = CFG_DFCO;
    put16(ecu.cal.bytes[0] + 0x630, 500);
}
static void test_idle_dfco(void) {
    EnginePlan p;
    u16 error, i, before;
    s32 integral;
    u8 *c;
    static const u8 invalid[] = {0, QUALITY_STALE, QUALITY_RANGE, QUALITY_CONFIG};
    idle_dfco_setup(); c = ecu.cal.bytes[0];
    CHECK(c[0x603] == 1 && get16(c + CAL_DFCO_EXIT_RPM) == 200);
    /* MAP, including a failed sensor, has no authority over overrun eligibility. */
    ecu.sensors.map.value = 200;
    ecu.sensors.map.quality = QUALITY_RANGE;
    c[0x5EB] = 0;
    p = idle_dfco_pass(1000, 2500);
    CHECK(ecu.control.throttle_closed && !ecu.control.dfco && ecu.control.dfco_waiting);
    CHECK(ecu.control.idle_mode == IDLE_FEEDBACK && ecu.control.idle_spark10 < 0);
    /* 0.0.8: feedback entry starts the integral from base position; the
       P term already holds the output at the closed end, so it stays. */
    integral = ecu.control.idle_integral;
    CHECK(integral == 0);
    p = idle_dfco_pass(1099, 2500);
    CHECK(!ecu.control.dfco && ecu.control.idle_integral <= integral);
    p = idle_dfco_pass(1100, 2500);
    CHECK(ecu.control.dfco && (p.fuel_cut & CUT_DFCO) && !p.spark_cut);
    CHECK(ecu.control.idle_mode == IDLE_RETURN && !ecu.control.idle_spark10);
    p = idle_dfco_pass(1110, 1151);
    CHECK(ecu.control.dfco);
    p = idle_dfco_pass(1120, 1150); /* idle target 950 + default 200 */
    CHECK(!ecu.control.dfco && !(p.fuel_cut & CUT_DFCO));
    CHECK(ecu.control.idle_mode == IDLE_FEEDBACK && ecu.control.idle_spark10 < 0);
    /* Entry is strict, independent of the restart margin. */
    c[0x603] = 0;
    p = idle_dfco_pass(1200, 1800); CHECK(!ecu.control.dfco);
    p = idle_dfco_pass(1210, 1801); CHECK(ecu.control.dfco);
    ecu.sensors.tps.value = 31;
    p = idle_dfco_pass(1220, 2500);
    CHECK(!ecu.control.throttle_closed && !ecu.control.dfco && !ecu.control.dfco_waiting);
    CHECK(ecu.control.idle_mode == IDLE_OFF && !ecu.control.idle_spark10);
    ecu.sensors.tps.value = 30;
    for (i = 0; i < sizeof(invalid); i++) {
        ecu.sensors.tps.quality = QUALITY_VALID;
        p = idle_dfco_pass(1300, 2500); CHECK(ecu.control.dfco);
        ecu.sensors.tps.quality = invalid[i];
        p = idle_dfco_pass(1310, 2500);
        CHECK(!ecu.control.throttle_closed && !ecu.control.dfco);
        CHECK(ecu.control.idle_mode == IDLE_UNAVAILABLE && !ecu.control.idle_spark10);
    }
    ecu.sensors.tps.quality = QUALITY_VALID;
    ecu.sensors.clt.value = 59;
    p = idle_dfco_pass(1400, 2500); CHECK(!ecu.control.dfco);
    ecu.sensors.clt.value = 60;
    p = idle_dfco_pass(1410, 2500); CHECK(ecu.control.dfco);
    ecu.sensors.clt.quality = QUALITY_STALE;
    p = idle_dfco_pass(1420, 2500); CHECK(!ecu.control.dfco);
    ecu.sensors.clt.quality = QUALITY_VALID;
    /* Configurable exit, legacy zero fallback, and no entry/exit oscillation
       when a margin or cold idle target exceeds the simple entry threshold. */
    put16(c + CAL_DFCO_EXIT_RPM, 500);
    p = idle_dfco_pass(1500, 2500); CHECK(ecu.control.dfco);
    p = idle_dfco_pass(1510, 1451); CHECK(ecu.control.dfco);
    p = idle_dfco_pass(1520, 1450); CHECK(!ecu.control.dfco);
    put16(c + CAL_DFCO_EXIT_RPM, 0);
    p = idle_dfco_pass(1530, 2500); CHECK(ecu.control.dfco);
    p = idle_dfco_pass(1540, 1150); CHECK(!ecu.control.dfco);
    put16(c + CAL_DFCO_EXIT_RPM, 2000);
    for (i = 0; i < 4; i++) {
        p = idle_dfco_pass(1600 + i * 10, 2500); CHECK(!ecu.control.dfco);
    }
    CHECK(cal_validate(c, &error));
    put16(c + CAL_DFCO_EXIT_RPM, 2001);
    CHECK(!cal_validate(c, &error) && error == CAL_DFCO_EXIT_RPM);
    put16(c + CAL_DFCO_EXIT_RPM, 200);
    c[0x5EB] = 255; CHECK(cal_validate(c, &error)); /* reserved byte ignored */
    /* Delay restarts after opening the throttle, including clock wrap. */
    c[0x603] = 1;
    p = idle_dfco_pass(0xFFFFFFD0UL, 2500); CHECK(!ecu.control.dfco);
    p = idle_dfco_pass(51, 2500); CHECK(!ecu.control.dfco);
    p = idle_dfco_pass(52, 2500); CHECK(ecu.control.dfco);
    ecu.sensors.tps.value = 31;
    p = idle_dfco_pass(53, 2500); CHECK(!ecu.control.dfco);
    ecu.sensors.tps.value = 30;
    p = idle_dfco_pass(54, 2500); CHECK(!ecu.control.dfco);
    p = idle_dfco_pass(153, 2500); CHECK(!ecu.control.dfco);
    p = idle_dfco_pass(154, 2500); CHECK(ecu.control.dfco);
    /* Disabled DFCO: PI closes from the held position even >500 RPM above
       target, spark retards, then advances below target. */
    c[0x5D4] = 0;
    p = idle_dfco_pass(2000, 2500);
    CHECK(!ecu.control.dfco && ecu.control.idle_mode == IDLE_FEEDBACK);
    before = ecu.control.idle_position;
    p = idle_dfco_pass(2010, 2500);
    CHECK(ecu.control.idle_position <= before && ecu.control.idle_spark10 < 0);
    p = idle_dfco_pass(2020, 800); CHECK(ecu.control.idle_spark10 > 0);
    /* Spark-only mode retains its configured air position at overspeed. */
    c[0x5D8] = 2;
    p = idle_dfco_pass(2030, 2500);
    CHECK(ecu.control.idle_position == 30 && ecu.control.idle_spark10 < 0);
    /* Live margin commits are permitted, with exact boundary validation. */
    CHECK(cal_begin()); put16(ecu.cal.bytes[1] + CAL_DFCO_EXIT_RPM, 600);
    CHECK(cal_commit(0));
    CHECK(get16(cal_active() + CAL_DFCO_EXIT_RPM) == 600);
}
/* Steps mode (0x5D8 = 0) walks one step per interval. A P gain left in the
   map (hidden by the Wizard in this mode) must not move the valve: on
   2026-10-07 it slewed the IAC 25<->75 steps at idle and stalled the engine. */
static void test_idle_step_mode(void) {
    u16 target, start, i;
    u8 *c;
    idle_dfco_setup(); c = ecu.cal.bytes[0];
    c[0x5D4] = 0;
    c[0x5D8] = 0;
    put16(c + 0x630, 5000); /* 0.5 steps/RPM, as in the 2026-10-07 basemap */
    put16(c + CAL_IDLE_STEP_MS, 100);
    idle_dfco_pass(1000, 950);
    target = ecu.control.idle_target;
    idle_dfco_pass(1010, target);
    CHECK(ecu.control.idle_mode == IDLE_FEEDBACK);
    start = ecu.control.idle_position;
    CHECK(start >= 20);
    /* 400 RPM over target for ~1 s: ten steps closed, no proportional jump. */
    for (i = 0; i < 100; i++)
        idle_dfco_pass(1100 + i * 10UL, (u16)(target + 400));
    CHECK(start - ecu.control.idle_position == 10);
    /* 400 RPM under target for 0.4 s: five steps open. */
    start = ecu.control.idle_position;
    for (i = 0; i < 41; i++)
        idle_dfco_pass(2100 + i * 10UL, (u16)(target - 400));
    CHECK(ecu.control.idle_position - start == 5);
}
/* A brief running sync loss resumes RUNNING with its original start time. */
static void test_sync_recovery(void) {
    u8 *c;
    idle_dfco_setup(); c = ecu.cal.bytes[0];
    ecu.control.running_at = 100;
    /* Zero window: today's behaviour, a loss is a new start. */
    idle_dfco_pass(5000, 2000);
    ecu.rotation.state = ROT_UNSYNCED;
    idle_dfco_pass(5010, 0);
    CHECK(ecu.control.mode == ENGINE_STOPPED && ecu.control.running_at == 5010);
    ecu.rotation.state = ROT_VALID;
    idle_dfco_pass(5020, 2000);
    /* 0.0.8: a new start, past twice the running threshold: RUNNING now. */
    CHECK(ecu.control.mode == ENGINE_RUNNING && ecu.control.running_at == 5020);
    /* Inside the window: straight back to RUNNING, start time kept. */
    idle_dfco_setup(); c = ecu.cal.bytes[0];
    put16(c + CAL_SYNC_RECOVERY_MS, 500);
    ecu.control.running_at = 100;
    idle_dfco_pass(5000, 2000);
    ecu.rotation.state = ROT_UNSYNCED;
    idle_dfco_pass(5010, 0);
    CHECK(ecu.control.mode == ENGINE_STOPPED && ecu.control.sync_recovering);
    ecu.rotation.state = ROT_ACQUIRING;
    idle_dfco_pass(5400, 0);
    CHECK(ecu.control.mode == ENGINE_STOPPED && ecu.control.running_at == 100);
    ecu.rotation.state = ROT_VALID;
    idle_dfco_pass(5510, 2000);
    CHECK(ecu.control.mode == ENGINE_RUNNING && ecu.control.running_at == 100);
    CHECK(!ecu.control.sync_recovering && ecu.control.idle_mode != IDLE_CRANK);
    /* Below cranking RPM on resync still drops to cranking. */
    ecu.rotation.state = ROT_UNSYNCED;
    idle_dfco_pass(6000, 0);
    ecu.rotation.state = ROT_VALID;
    idle_dfco_pass(6100, 300);
    CHECK(ecu.control.mode == ENGINE_CRANKING);
    /* Past the window: a new start. */
    idle_dfco_setup(); c = ecu.cal.bytes[0];
    put16(c + CAL_SYNC_RECOVERY_MS, 500);
    ecu.control.running_at = 100;
    idle_dfco_pass(5000, 2000);
    ecu.rotation.state = ROT_UNSYNCED;
    idle_dfco_pass(5010, 0);
    idle_dfco_pass(5511, 0);
    CHECK(!ecu.control.sync_recovering && ecu.control.running_at == 5511);
    ecu.rotation.state = ROT_VALID;
    idle_dfco_pass(5520, 2000);
    CHECK(ecu.control.mode == ENGINE_RUNNING && ecu.control.running_at == 5520);
    /* Key-off ends the recovery; a stopped or cranking loss never arms it. */
    idle_dfco_setup(); c = ecu.cal.bytes[0];
    put16(c + CAL_SYNC_RECOVERY_MS, 500);
    ecu.rotation.state = ROT_UNSYNCED;
    idle_dfco_pass(5010, 0);
    ecu.control.key_on = 0;
    idle_dfco_pass(5020, 0);
    CHECK(!ecu.control.sync_recovering);
    ecu.control.key_on = 1;
    ecu.control.mode = ENGINE_CRANKING;
    idle_dfco_pass(5030, 0);
    CHECK(!ecu.control.sync_recovering);
}
/* 0.0.8 engine state: start flare and rpm dips (log nieuwedag.csv). */
static void test_engine_state_009(void) {
    idle_dfco_setup();
    ecu.control.mode = ENGINE_STOPPED;
    ecu.control.running_at = 0;
    /* Below twice the 650 rpm threshold the 300 ms qualification holds. */
    idle_dfco_pass(1000, 700);
    CHECK(ecu.control.mode == ENGINE_CRANKING && ecu.control.qualifying);
    idle_dfco_pass(1100, 1299);
    CHECK(ecu.control.mode == ENGINE_CRANKING);
    /* At 1300 rpm the start flare ends cranking fuel at once. */
    idle_dfco_pass(1110, 1300);
    CHECK(ecu.control.mode == ENGINE_RUNNING && ecu.control.running_at == 1110);
    /* A dip below 450 rpm within the first second is a failed start. */
    idle_dfco_pass(1500, 400);
    CHECK(ecu.control.mode == ENGINE_CRANKING && !ecu.control.dip_recovering);
    idle_dfco_pass(1510, 700);
    CHECK(ecu.control.mode == ENGINE_CRANKING);
    idle_dfco_pass(1810, 700);
    CHECK(ecu.control.mode == ENGINE_RUNNING && ecu.control.running_at == 1810);
    /* An established engine dipping to 374 rpm gets cranking fuel, then
       RUNNING straight back at 650 rpm with its start time kept, so no
       second after-start enrichment or start catch. */
    idle_dfco_pass(30000, 374);
    CHECK(ecu.control.mode == ENGINE_CRANKING && ecu.control.dip_recovering);
    CHECK(ecu.control.idle_mode == IDLE_CRANK);
    idle_dfco_pass(30010, 600);
    CHECK(ecu.control.mode == ENGINE_CRANKING);
    idle_dfco_pass(30020, 650);
    CHECK(ecu.control.mode == ENGINE_RUNNING && ecu.control.running_at == 1810);
    CHECK(!ecu.control.dip_recovering && ecu.control.idle_mode != IDLE_CATCH);
    /* A stall ends the dip: the next sync is a normal start. */
    idle_dfco_pass(40000, 374);
    CHECK(ecu.control.dip_recovering);
    ecu.rotation.state = ROT_UNSYNCED;
    idle_dfco_pass(40100, 0);
    CHECK(ecu.control.mode == ENGINE_STOPPED && !ecu.control.dip_recovering);
    ecu.rotation.state = ROT_VALID;
    idle_dfco_pass(41000, 700);
    CHECK(ecu.control.mode == ENGINE_CRANKING);
}
/* Idle spark correction stops above idle target + the calibrated margin. */
static void test_idle_spark_rpm(void) {
    u8 *c;
    idle_dfco_setup(); c = ecu.cal.bytes[0];
    c[0x603] = 50; /* keep DFCO waiting */
    idle_dfco_pass(1000, 2500);
    CHECK(ecu.control.idle_mode == IDLE_FEEDBACK && ecu.control.idle_spark10 < 0);
    put16(c + CAL_IDLE_SPARK_RPM, 300);
    idle_dfco_pass(1010, 2500);
    CHECK(ecu.control.idle_mode == IDLE_FEEDBACK && !ecu.control.idle_spark10);
    idle_dfco_pass(1020, 1251); /* target 950 + 300 */
    CHECK(!ecu.control.idle_spark10);
    idle_dfco_pass(1030, 1250);
    CHECK(ecu.control.idle_spark10 < 0);
    idle_dfco_pass(1040, 800);
    CHECK(ecu.control.idle_spark10 > 0);
    put16(c + CAL_IDLE_SPARK_RPM, 3001);
    CHECK(!cal_validate(c, &ecu.cal.error_offset) && ecu.cal.error_offset == CAL_IDLE_SPARK_RPM);
    put16(c + CAL_IDLE_SPARK_RPM, 0);
    put16(c + CAL_SYNC_RECOVERY_MS, 2001);
    CHECK(!cal_validate(c, &ecu.cal.error_offset) && ecu.cal.error_offset == CAL_SYNC_RECOVERY_MS);
}
/* A clean tool session releases SERVICE and its own deadline. */
static void service_pass(u32 now) {
    ecu.foreground_stamp = now - 10U;
    service_poll(now);
}
static void test_service_release(void) {
    u32 now;
    setup();
    memset(fake_eeprom, 0xFF, 8192);
    ecu.control.key_on = 1;
    CHECK(storage_save(0));
    CHECK(ecu.service && (ecu.authority.inhibits & INH_SERVICE));
    safety_inhibit(INH_DEADLINE, 1); /* storage stall during the session */
    service_pass(2);
    CHECK(ecu.service); /* save still running */
    for (now = 0; ecu.storage.phase && now < 1000; now++)
        storage_poll(now);
    CHECK(ecu.storage.result == 1);
    /* Not on the pass straight after a stall. */
    ecu.foreground_stamp = 900;
    service_poll(1000);
    CHECK(ecu.service);
    service_pass(1010);
    CHECK(!ecu.service && !(ecu.authority.inhibits & (INH_SERVICE | INH_DEADLINE)));
    CHECK(ecu.iac.state == IAC_HOMING);
    safety_conditions(INH_SYNC, 1020);
    CHECK(ecu.authority.inhibits == INH_SYNC);
    /* 0.0.8: DEADLINE is a per-pass condition (the OEM has no timing
       latch); a deadline from before the session clears with it. */
    setup();
    memset(fake_eeprom, 0xFF, 8192);
    safety_inhibit(INH_DEADLINE, 0);
    CHECK(storage_save(0));
    for (now = 0; ecu.storage.phase && now < 1000; now++)
        storage_poll(now);
    service_pass(1010);
    CHECK(!ecu.service && !(ecu.authority.inhibits & (INH_SERVICE | INH_DEADLINE)));
    /* Rotation during the session keeps SERVICE until reset; the deadline
       still clears on the next complete foreground evaluation. */
    setup();
    memset(fake_eeprom, 0xFF, 8192);
    CHECK(storage_save(0));
    ecu.rotation.state = ROT_ACQUIRING;
    service_pass(5);
    ecu.rotation.state = ROT_UNSYNCED;
    safety_inhibit(INH_DEADLINE, 6);
    for (now = 0; ecu.storage.phase && now < 1000; now++)
        storage_poll(now);
    service_pass(1010);
    CHECK(ecu.service && (ecu.authority.inhibits & (INH_SERVICE | INH_DEADLINE)) ==
                             (INH_SERVICE | INH_DEADLINE));
    safety_conditions(0, 1020);
    CHECK(ecu.authority.inhibits == INH_SERVICE);
}
/* Firmware 0.0.8: idle feedback only near target, entry from base, retard
   limit while falling, DFCO coolant. */
static void test_idle_008(void) {
    u8 *c;
    u16 base;
    idle_dfco_setup(); c = ecu.cal.bytes[0];
    c[0x5D4] = 0; /* no DFCO: coasting must still not use feedback */
    put16(c + CAL_IDLE_FEEDBACK_RPM, 300);
    idle_dfco_pass(1000, 2500);
    CHECK(ecu.control.idle_mode == IDLE_RETURN && !ecu.control.idle_spark10);
    base = ecu.control.idle_position;
    idle_dfco_pass(1010, 2500); /* dashpot: one step per pass towards base */
    CHECK(ecu.control.idle_position == base - 1U || ecu.control.idle_position == base);
    idle_dfco_pass(1020, 1249); /* target 950 + 300, exclusive */
    CHECK(ecu.control.idle_mode == IDLE_FEEDBACK && ecu.control.idle_integral <= 0);
    /* Retard limit while falling: 2 deg at most (0.5 deg units). */
    c[CAL_IDLE_RETARD_FALLING] = 4;
    put16(c + 0x630, 500);
    idle_dfco_pass(70000, 1240);
    idle_dfco_pass(70100, 1240);
    CHECK(!ecu.control.idle_falling && ecu.control.idle_spark10 < -20);
    idle_dfco_pass(70200, 1200);
    CHECK(ecu.control.idle_falling && ecu.control.idle_spark10 == -20);
    /* DFCO coolant threshold: zero keeps 60 C, otherwise calibrated. */
    idle_dfco_setup(); c = ecu.cal.bytes[0];
    c[0x603] = 0;
    ecu.sensors.clt.value = 45;
    idle_dfco_pass(1000, 2500); CHECK(!ecu.control.dfco);
    c[CAL_DFCO_MIN_CLT] = 40;
    idle_dfco_pass(1010, 2500); CHECK(ecu.control.dfco);
    c[CAL_DFCO_MIN_CLT] = 50;
    idle_dfco_pass(1020, 2500); CHECK(!ecu.control.dfco);
    /* Validation of the new fields. */
    c[CAL_DFCO_MIN_CLT] = 151;
    CHECK(!cal_validate(c, &ecu.cal.error_offset) && ecu.cal.error_offset == CAL_DFCO_MIN_CLT);
    c[CAL_DFCO_MIN_CLT] = 0;
    put16(c + CAL_CRANK_DWELL_US, 999);
    CHECK(!cal_validate(c, &ecu.cal.error_offset) && ecu.cal.error_offset == CAL_CRANK_DWELL_US);
    put16(c + CAL_CRANK_DWELL_US, CRANK_DWELL_MAX_US + 1U);
    CHECK(!cal_validate(c, &ecu.cal.error_offset));
    put16(c + CAL_CRANK_DWELL_US, CRANK_DWELL_MAX_US);
    CHECK(cal_validate(c, &ecu.cal.error_offset));
    c[CAL_IDLE_RETARD_FALLING] = 41;
    CHECK(!cal_validate(c, &ecu.cal.error_offset) && ecu.cal.error_offset == CAL_IDLE_RETARD_FALLING);
    c[CAL_IDLE_RETARD_FALLING] = 0;
    put16(c + CAL_IDLE_FEEDBACK_RPM, 3001);
    CHECK(!cal_validate(c, &ecu.cal.error_offset) && ecu.cal.error_offset == CAL_IDLE_FEEDBACK_RPM);
}
/* Firmware 0.0.8: an output latch clears after a stall, three times per
   key cycle, and the live frame version 5 reports it. */
static void test_latch_recovery(void) {
    extern u16 fake_crank_activity;
    static const u8 v5[] = {0xAA, 2, 0x13, 0x05, 0x1A};
    u32 now = 1000;
    u8 i;
    setup();
    ecu.rotation.state = ROT_UNSYNCED;
    for (i = 0; i < LATCH_CLEARS_PER_KEY; i++) {
        latch_poll(now - 10); /* every foreground pass polls */
        safety_trip(INH_OUTPUT, TRIP_COIL_WATCHDOG, now);
        latch_poll(now);
        latch_poll(now + 980); /* quiet counts from the last edge */
        CHECK(ecu.authority.inhibits & INH_OUTPUT);
        fake_crank_activity++; /* an edge restarts the quiet period */
        latch_poll(now + 1000);
        latch_poll(now + 1999);
        CHECK(ecu.authority.inhibits & INH_OUTPUT);
        latch_poll(now + 2000);
        CHECK(!(ecu.authority.inhibits & INH_OUTPUT) && ecu.authority.latch_clears == i + 1U);
        now += 5000;
    }
    safety_trip(INH_OUTPUT, TRIP_COIL_WATCHDOG, now);
    latch_poll(now); latch_poll(now + 5000);
    CHECK((ecu.authority.inhibits & INH_OUTPUT) && ecu.authority.latch_clears == 3);
    /* Not while the crank turns, and outputs must read back off first. */
    ecu.authority.latch_clears = 0;
    ecu.rotation.rpm = 100;
    latch_poll(now + 6000); latch_poll(now + 8000);
    CHECK(ecu.authority.inhibits & INH_OUTPUT);
    ecu.rotation.rpm = 0;
    fake_coils[0] = 1;
    latch_poll(now + 9000); /* quiet since the 100 rpm pass at +8000 */
    CHECK((ecu.authority.inhibits & INH_OUTPUT) && !fake_coils[0]);
    latch_poll(now + 9010);
    CHECK(!(ecu.authority.inhibits & INH_OUTPUT));
    /* Live frame 5: trip source, clears, and the per-event counters. */
    ecu.authority.coil_timeouts = 7; ecu.authority.injector_clamps = 2;
    ecu.authority.late_events = 9;
    feed(v5, sizeof(v5), now + 10020);
    CHECK(ecu.protocol.tx[1] == 72 && ecu.protocol.tx[2] == 5);
    CHECK(ecu.protocol.tx[2 + 64] == TRIP_COIL_WATCHDOG && ecu.protocol.tx[2 + 65] == 1);
    CHECK(get16(ecu.protocol.tx + 2 + 66) == 7 && get16(ecu.protocol.tx + 2 + 68) == 2 &&
          get16(ecu.protocol.tx + 2 + 70) == 9);
    /* DEADLINE is a condition: the next evaluation clears it, OUTPUT stays. */
    safety_trip(INH_OUTPUT, TRIP_COIL_REFUSED, now);
    safety_trip(INH_DEADLINE, TRIP_FOREGROUND_GAP, now);
    safety_conditions(0, now);
    CHECK(ecu.authority.inhibits == INH_OUTPUT && ecu.authority.trip_source == TRIP_FOREGROUND_GAP);
}
#include "knock.h"
/* 0.0.8 live frame 6: full-range oxygen input, enrichment-adjusted target
   and knock fallback. All versions report the plan of the last control pass,
   even while an inhibit refuses publication (log nieuwedag.csv: 18.9 ms and
   17.5 deg frozen with the outputs off). */
static void test_live_frame_v6(void) {
    static const u8 v6[] = {0xAA, 2, 0x13, 0x06, 0x1B};
    static const u8 v5[] = {0xAA, 2, 0x13, 0x05, 0x1A};
    static const u8 v7[] = {0xAA, 2, 0x13, 0x07, 0x1C};
    setup();
    ecu.authority.plan.pulse_us = 18900; ecu.authority.plan.advance10 = 175;
    ecu.authority.inhibits = INH_DEADLINE;
    ecu.control.plan_pulse_us = 3100; ecu.control.plan_advance10 = 245;
    ecu.control.plan_fuel_cut = CUT_DFCO;
    ecu.sensors.oxygen_mv = 3550;
    ecu.control.mode = ENGINE_RUNNING; ecu.control.target_afr = 147;
    ecu.control.warmup = 113; ecu.control.afterstart = 130; ecu.control.ae_percent = 100;
    feed(v6, sizeof(v6), 100);
    CHECK(ecu.protocol.tx[1] == 76 && ecu.protocol.tx[2] == 6);
    CHECK(get16(ecu.protocol.tx + 2 + 17) == 3100 && get16(ecu.protocol.tx + 2 + 19) == 245);
    CHECK(ecu.protocol.tx[2 + 32] & 2);
    CHECK(ecu.protocol.tx[2 + 11] == 255 && get16(ecu.protocol.tx + 2 + 72) == 3550);
    CHECK(ecu.protocol.tx[2 + 12] == 147 && ecu.protocol.tx[2 + 74] == 100); /* 14.7/1.469 */
    CHECK(ecu.protocol.tx[2 + 75] == 0);
    while (ecu.protocol.tx_length) protocol_poll(100);
    /* Cranking VE replaces the cell: the target is reported unadjusted. */
    ecu.control.mode = ENGINE_CRANKING;
    knock.mode = KNOCK_CONTROL; knock.fallback = 1;
    feed(v6, sizeof(v6), 110);
    CHECK(ecu.protocol.tx[2 + 74] == 147 && ecu.protocol.tx[2 + 75] == 1);
    while (ecu.protocol.tx_length) protocol_poll(110);
    feed(v5, sizeof(v5), 120);
    CHECK(ecu.protocol.tx[1] == 72 && get16(ecu.protocol.tx + 2 + 17) == 3100);
    while (ecu.protocol.tx_length) protocol_poll(120);
    feed(v7, sizeof(v7), 130);
    CHECK(ecu.protocol.tx[1] == 1);
    while (ecu.protocol.tx_length) protocol_poll(130);
    knock.fallback = 0;
}
