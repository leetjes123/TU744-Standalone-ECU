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
    integral = ecu.control.idle_integral;
    p = idle_dfco_pass(1099, 2500);
    CHECK(!ecu.control.dfco && ecu.control.idle_integral < integral);
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
