/* Scripted peripheral-owner invariants; live IRQ/timing tests are separate. */
#include "ecu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define LRE_BOARD_H
#define BOARD_RELEASED 1
#define IRQ_HANDLER(vector)
Ecu ecu;
static u16 CCM0, CCM1, CCM5, CCM6, CCM7, T0, T1, T7;
static u16 CC0, CC1, CC2, CC4, CC6, CC9, CC20, CC21, CC23, CC27, CC28, CC29, CC30;
static u16 CC0IR, CC1IR, CC2IR, CC4IR, CC6IR, CC9IR, CC9IE;
static u16 CC20IR, CC21IR, CC23IR, CC27IR, CC28IR, CC29IR, CC30IR;
static u8 PIN_COIL_A, PIN_COIL_B, PIN_INJ_1, PIN_INJ_4, PIN_INJ_3, PIN_INJ_2, PIN_TACH;
/* The OEM pass reads the coil port: a high pin is an idle coil. */
#define P2 ((u16)((PIN_COIL_A ? 1U : 0U) | (PIN_COIL_B ? 2U : 0U)))
static u16 irq_enabled, captured_value, period_value;
volatile u16 capture_schedule_counter;
static u32 clock_value;
static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
u16 hal_lock(void) { u16 old = irq_enabled; irq_enabled = 0; return old; }
void hal_unlock(u16 old) { irq_enabled = old; }
u32 hal_capture_clock(void) { return clock_value; }
u16 board_recent_period(u16 *captured) { *captured = captured_value; return period_value; }
u8 board_capture_snapshot(u16 *counter, u16 *captured, u16 *period) {
    *counter = T0; *period = board_recent_period(captured); return 1;
}
void hal_phase_arm(void) { }
void hal_phase_disarm(void) { }
void diagnostic_phase_arm(u32 stamp, u16 epoch, u8 tooth) {
    (void)stamp; (void)epoch; (void)tooth;
}
void diagnostic_rotation_ineligible(void) {}
void diagnostic_rotation_edge(u32 stamp, u16 epoch, u8 tooth, u8 gap, u8 valid) {
    (void)stamp; (void)epoch; (void)tooth; (void)gap; (void)valid;
}
void iac_disable(void) { }
void knock_invalidate(u8 reason) { (void)reason; }
s16 knock_advance(s16 base) { return base > 127 ? 127 : (base < -128 ? -128 : base); }
#include "../target/c167/engine_outputs.c"

static void setup(void) {
    memset(&ecu, 0, sizeof(ecu));
    CCM0 = CCM1 = CCM5 = CCM6 = CCM7 = T0 = T1 = T7 = 0;
    clock_value = captured_value = capture_schedule_counter = 0;
    period_value = 1000; irq_enabled = 1;
    board_outputs_init();
    ecu.rotation.state = ROT_VALID; ecu.rotation.normal = period_value;
    ecu.control.mode = ENGINE_RUNNING;
    ecu.authority.epoch = ecu.authority.plan.epoch = 1;
    ecu.authority.plan.max_age_ms = 1000;
    ecu.authority.plan.dwell_us = 500; ecu.authority.plan.pulse_us = 1000;
    ecu.authority.plan.dwell_feedback = 1;
    ecu.authority.plan.trigger10 = 1140;
    ecu.authority.plan.injection_phase10 = 840;
}
/* One OEM segment pass at boundary tooth 1/31 with its capture at stamp. */
static void segment(u8 tooth, u32 stamp, u16 counter) {
    clock_value = stamp; T1 = T7 = (u16)stamp; T0 = counter;
    capture_schedule_counter = counter;
    board_ignition_segment(tooth, stamp, counter, period_value);
}
/* Tooth period 1000 ticks -> 125 ticks per count before a measured segment;
   trigger 114.0, advance 0 -> fire 144 counts after the tooth-1 boundary. */
static void test_ignition(void) {
    EnginePlan cut;
    setup(); segment(1, 0, 0);
    /* 500 us = 625 ticks = 5 counts: slot 0 charge at 139 counts, coarse tooth
       17 + 3 substeps (CC6 path); fire tooth 17 + 8 substeps, kept until the
       coil charges. */
    CHECK(start_stage[0].mode == STAGE_COARSE && (CCM1 & 0xF00U) == 0x400U && CC6 == 17U);
    CHECK(start_stage[0].fraction == 3U && start_stage[0].cc6);
    CHECK(fire_stage[0].mode == STAGE_COARSE && fire_stage[0].value == 17U && fire_stage[0].fraction == 8U);
    CHECK(fire_pending[0] && PIN_COIL_A && !ecu.authority.coil_active[0]);
    T0 = 17; captured_value = 17000; T1 = captured_value; coil0_isr();
    CHECK((CCM1 & 0xF00U) == 0xC00U && CC6 == 17375U);    /* ROM fine: 3 x 125 */
    clock_value = T1 = T7 = CC6; coil0_isr();
    CHECK(!PIN_COIL_A && ecu.authority.coil_active[0] && CC9IE);
    CHECK((CCM0 & 15U) == 4U && CC0 == 17U && CC0IR);     /* stored coarse fire installed */
    CC0IR = 0; spark0_isr();
    CHECK((CCM0 & 15U) == 13U && CC0 == 18000U);           /* 8 x 125: toggle on T1 */
    T1 = CC9 = (u16)(charge_at[0] + 450U); dwell_feedback_isr();
    CHECK(feedback_seen[0] && feedback_interval[0] == 450 && !CC9IE);
    T1 = CC0; spark0_isr();
    CHECK(PIN_COIL_A && !ecu.authority.coil_active[0] && feedback_ready[0] && !fire_pending[0]);
    CHECK(!(CCM0 & 15U) && !(CCM5 & 15U));

    /* Revocation: nothing armed survives; no pass while inhibited. */
    setup(); segment(1, 0, 0); safety_inhibit(INH_SYNC, 0);
    coil0_isr(); spark0_isr();
    CHECK(PIN_COIL_A && PIN_COIL_B && !(CCM0 & 0x0FFU) && !(CCM1 & 0xF0FU));
    CHECK(start_stage[0].mode == STAGE_NONE && fire_stage[0].mode == STAGE_NONE && !CC9IE);
    segment(31, 30000, 30);
    CHECK(start_stage[0].mode == STAGE_NONE && start_stage[1].mode == STAGE_NONE);
    safety_conditions(0, 0); ecu.authority.plan.epoch = ecu.authority.epoch;
    segment(1, 60000, 58); CHECK(start_stage[0].mode == STAGE_COARSE);
    cut = ecu.authority.plan; cut.spark_cut = CUT_REV; cut.fuel_cut = CUT_REV;
    CHECK(safety_publish(&cut));
    CHECK(start_stage[0].mode == STAGE_NONE && !(CCM1 & 0xF0FU) && !(CCM7 & 0xFFFU));

    /* A coarse charge serviced two teeth late is dropped and counted. */
    setup(); segment(1, 0, 0); T0 = 19; coil0_isr();
    CHECK(PIN_COIL_A && !ecu.authority.inhibits && ecu.authority.late_events == 1);
    /* Soft spark cut: the shared owner refuses the charge without a fault. */
    setup(); segment(1, 0, 0); ecu.authority.plan.soft_spark = 100;
    T0 = 17; T1 = captured_value = 17000; coil0_isr();
    clock_value = T1 = T7 = CC6; coil0_isr();
    CHECK(PIN_COIL_A && start_stage[0].mode == STAGE_NONE && !ecu.authority.inhibits);
}
static void test_injection(void) {
    setup(); board_schedule(0);
    CHECK(injector_phase[0] == 1 && PIN_INJ_1 && PIN_INJ_4);
    CHECK(CC30 == 14000 && CC28 == 14000);
    T7 = CC30; PIN_INJ_1 = PIN_INJ_4 = 0; /* hardware start */
    injector0_isr(); injector1_isr();
    CHECK(CC30 == 15250 && CC28 == 15250 && injector_phase[0] == 2);
    T7 = CC30; PIN_INJ_1 = PIN_INJ_4 = 1; /* hardware end */
    injector0_isr(); injector1_isr();
    CHECK(!ecu.authority.injector_active[0] && !(CCM7 & 0xF0FU));
    setup(); board_schedule(0); safety_inhibit(INH_SYNC, 0);
    CHECK(PIN_INJ_1 && PIN_INJ_4 && !(CCM7 & 0xFFFU));
}
static void queue_injection(u32 origin) {
    setup();
    ecu.authority.plan.dwell_us = 0; /* isolate the injector owner */
    ecu.rotation.normal = period_value = 500;
    ecu.authority.plan.pulse_us = 20000; /* 83.3% of a 24 ms revolution */
    clock_value = origin; T7 = (u16)origin;
    board_schedule(origin);
    CHECK(CC30 == (u16)(origin + 7000UL));
    clock_value = origin + 7000UL; T7 = (u16)clock_value;
    PIN_INJ_1 = PIN_INJ_4 = 0; injector0_isr(); injector1_isr();
    CHECK(CC30 == (u16)(origin + 32000UL));
    ecu.rotation.tooth = 30; capture_schedule_counter = 30;
    clock_value = origin + 15000UL; T7 = (u16)clock_value;
    board_schedule(clock_value);
    CHECK(pending_injection[0].ready && pending_injection[0].clock == origin + 37000UL);
    CHECK(CC30 == (u16)(origin + 32000UL) && injector_phase[0] == 2);
    /* Repeated scheduling cannot overwrite or consume the queued start. */
    board_schedule(clock_value);
    CHECK(!ecu.authority.inhibits && pending_injection[0].ready);
}
static void finish_pair(u32 clock) {
    clock_value = clock; T7 = (u16)clock;
    PIN_INJ_1 = 1; injector0_isr();
    CHECK(pending_injection[0].ready && !ecu.authority.injector_active[0]);
    PIN_INJ_4 = 1; injector1_isr();
}
static void test_pending_injection(void) {
    static const u32 origins[] = {0, 50000UL, 0xFFFFC000UL};
    unsigned i;
    EnginePlan cut;
    for (i = 0; i < sizeof(origins) / sizeof(origins[0]); i++) {
        u32 origin = origins[i];
        queue_injection(origin); finish_pair(origin + 32000UL);
        CHECK(!ecu.authority.inhibits && !pending_injection[0].ready);
        CHECK(injector_phase[0] == 1 && injector_phase[1] == 1);
        CHECK(CC30 == (u16)(origin + 37000UL) && CC28 == CC30);
        CHECK(PIN_INJ_1 && PIN_INJ_4 && ecu.authority.injector_active[0]);
        clock_value = origin + 37000UL; T7 = (u16)clock_value;
        PIN_INJ_1 = PIN_INJ_4 = 0; injector0_isr(); injector1_isr();
        CHECK(CC30 == (u16)(origin + 62000UL) && CC28 == CC30);
    }
    queue_injection(0); ecu.authority.plan.soft_fuel = 100;
    finish_pair(32000);
    CHECK(!pending_injection[0].ready && !ecu.authority.injector_active[0]);
    CHECK(ecu.authority.fuel_accumulator[0] == 0 && !ecu.authority.inhibits);
    queue_injection(0); ecu.milliseconds = 1001; finish_pair(32000);
    CHECK(!pending_injection[0].ready && !ecu.authority.injector_active[0]);
    queue_injection(0); ecu.authority.epoch++; finish_pair(32000);
    CHECK(!pending_injection[0].ready && !ecu.authority.injector_active[0]);
    queue_injection(0); finish_pair(37000);
    CHECK((ecu.authority.inhibits & INH_DEADLINE) && !pending_injection[0].ready);
    CHECK(PIN_INJ_1 && PIN_INJ_4 && !(CCM7 & 0xFFFU));
    queue_injection(0); safety_inhibit(INH_SYNC, 0);
    CHECK(!pending_injection[0].ready && !ecu.authority.injector_active[0]);
    queue_injection(0); cut = ecu.authority.plan; cut.fuel_cut = CUT_REV;
    CHECK(safety_publish(&cut) && !pending_injection[0].ready);
    CHECK(PIN_INJ_1 && PIN_INJ_4 && !(CCM7 & 0xFFFU));
}
static void test_fine_and_reschedule(void) {
    u16 at;
    /* ROM fine stage on the latest interval; an implausible one revokes angle. */
    setup(); period_value = 1000; captured_value = 9000; T0 = 5;
    CHECK(fine(8, 0, 0, 5, &at) && at == 10000);
    CHECK(fine(3, 100, 1, 5, &at) && at == 9275);          /* 3 x 125 - 100 */
    CHECK(fine(1, 100, 1, 5, &at) && at == 9250);          /* max(125 - 100, 2 x 125) */
    setup(); period_value = 3000; T0 = 5;
    CHECK(!fine(8, 0, 0, 5, &at) && (ecu.authority.inhibits & INH_SYNC));
    /* A lost charge compare is not a fault: the next pass cancels and
       reprograms both charge compares (38036), as the ROM does. */
    setup(); segment(1, 0, 0);
    CCM1 &= 0xF0FFU;                                       /* compare lost */
    segment(31, 30000, 30);
    CHECK(!ecu.authority.inhibits && start_stage[0].mode == STAGE_NONE);
    segment(1, 60000, 58);
    CHECK(!ecu.authority.inhibits && start_stage[0].mode == STAGE_COARSE && CC6 == 75U);
}
static void charge_first_coil(void) {
    setup(); ecu.authority.plan.dwell_us = 3000;
    segment(1, 0, 0);
    T0 = CC6; T1 = captured_value = (u16)(T0 * period_value);
    coil0_isr();
    clock_value = T1 = T7 = CC6; coil0_isr();
    CHECK(!PIN_COIL_A && ecu.authority.coil_active[0]);
}
static void test_charge_revocation(void) {
    static const u16 reasons[] = {INH_SYNC, INH_STALE, INH_POWER, INH_CAL, INH_SERVICE, INH_DEADLINE};
    unsigned i;
    for (i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++) {
        u16 original_fire;
        charge_first_coil(); original_fire = active_fire_at[0];
        safety_inhibit(reasons[i], 0);
        CHECK(!PIN_COIL_A && ecu.authority.coil_active[0] && ecu.authority.spark_draining == 1);
        CHECK(start_stage[0].mode == STAGE_NONE && start_stage[1].mode == STAGE_NONE);
        CHECK(!CC9IE && !coil_admit(1, ecu.authority.epoch, 3000));
        if (reasons[i] & (INH_SYNC | INH_DEADLINE))
            CHECK(active_fire_timed[0] && CC0 == original_fire);
        if (!active_fire_timed[0]) {
            T0 = active_fire_counter[0]; T1 = captured_value = (u16)(T0 * period_value);
            spark0_isr();
            CHECK(!PIN_COIL_A);
        }
        T1 = CC0; spark0_isr();
        CHECK(PIN_COIL_A && !ecu.authority.coil_active[0] && !ecu.authority.spark_draining);
        CHECK(!feedback_ready[0] && !(CCM5 & 15U));
    }
    charge_first_coil(); safety_inhibit(INH_OUTPUT, 0);
    CHECK(PIN_COIL_A && !ecu.authority.coil_active[0] && !ecu.authority.spark_draining);
    charge_first_coil();
    { EnginePlan cut = ecu.authority.plan; cut.spark_cut = CUT_REV;
      CHECK(safety_publish(&cut) && !PIN_COIL_A && ecu.authority.spark_draining == 1); }
    CHECK(!service_enter(0));
}
static void test_feedback_policy(void) {
    charge_first_coil();
    CC9 = (u16)(charge_at[0] - 1U); dwell_feedback_isr();
    CHECK(!feedback_seen[0] && feedback_bad[0] && ecu.authority.feedback_invalid[0] == 1);
    setup(); ecu.authority.plan.dwell_us = 3000;
    dwell_state[0].correction = 1875; dwell_state[0].duration = 5625; dwell_base[0] = 3750;
    feedback_ready[0] = 1; completed_missing[0] = 1; completed_bad[0] = completed_fallback[0] = 0;
    board_schedule(0);
    CHECK(dwell_state[0].correction == 0 && ecu.authority.feedback_missing[0] == 1);
    CHECK(next_dwell[0] == 3750 && ecu.authority.feedback_correction[0] == 0);
    setup(); ecu.authority.plan.dwell_feedback = 0;
    dwell_state[0].correction = 1875; dwell_state[0].duration = 2500; dwell_base[0] = 625;
    board_schedule(0);
    CHECK(next_dwell[0] == 625 && !ecu.authority.feedback_correction[0]);
    CHECK(!ecu.authority.feedback_missing[0]);
    setup(); segment(1, 0, 0); T0 = (u16)(CC6 + 2U); coil0_isr();
    CHECK(start_stage[0].mode == STAGE_NONE && !ecu.authority.inhibits && ecu.authority.late_events == 1);
}
static void test_split(void) {
    u32 n;
    OemAngleStage s;
    for (n = 0; n <= 65535UL; n++) {
        oem_angle_split((u16)n, &s);
        CHECK((u32)s.teeth * 8UL + s.fraction == n);
        CHECK(s.fraction <= 9 && (!s.teeth || s.fraction >= 2));
        CHECK(us_ticks((u16)n) == (u16)((n * 5UL + 3UL) / 4UL));
    }
}

static void test_rotation_boundaries(void) {
    u32 normal, dt, stamp = 0x30UL;
    u32 points[9];
    unsigned i, state;
    s16 offset;
    u8 noise, gap, irregular, expected;
    /* Independent widened ratios exercise both sides of every decoder
       threshold, the 7281 fast-path boundary, timeout and timestamp wrap. */
    for (normal = 0; normal <= 7283UL; normal++) {
        points[0] = 100; points[1] = normal / 2;
        points[2] = normal * 5 / 2; points[3] = normal * 9 / 2;
        points[4] = normal * 3 / 5; points[5] = normal * 9 / 5;
        points[6] = 7281; points[7] = 250000; points[8] = 12500;
        for (i = 0; i < 9; i++) for (offset = -1; offset <= 1; offset++) {
            if (!points[i] && offset < 0) continue;
            dt = points[i] + (u32)(s32)offset;
            noise = (u8)(dt < 100 || dt < normal / 2);
            gap = (u8)(normal && dt * 2 > normal * 5 && dt * 2 < normal * 9);
            irregular = (u8)(normal && (dt * 5 < normal * 3 || dt * 5 > normal * 9));
            for (state = ROT_UNSYNCED; state <= ROT_VALID; state++) {
                memset(&ecu.rotation, 0, sizeof(ecu.rotation));
                ecu.rotation.state = (u8)state;
                ecu.rotation.seen = 1; ecu.rotation.tooth = 13;
                ecu.rotation.normal = normal; ecu.rotation.last = stamp - dt;
                ecu.authority.inhibits = 0;
                expected = noise ? (u8)state : dt > 250000 ? ROT_UNSYNCED :
                    gap ? ROT_ACQUIRING : irregular ? ROT_UNSYNCED : (u8)state;
                CHECK(rotation_edge(stamp) == (u8)(!noise && expected == ROT_VALID));
                CHECK(ecu.rotation.state == expected);
                CHECK(ecu.rotation.last == (noise ? stamp - dt : stamp));
                CHECK(ecu.rotation.normal == (noise ? normal : dt > 250000 ? 0 : gap ? normal : dt));
                CHECK(ecu.rotation.tooth == (noise || dt > 250000 ? 13 : gap ? 0 : 14));
            }
        }
    }
}

int main(void) {
    test_ignition(); test_injection(); test_pending_injection(); test_fine_and_reschedule();
    test_charge_revocation(); test_feedback_policy();
    test_split(); test_rotation_boundaries();
    printf("PASS %u staged-output/decoder assertions\n", checks);
    return 0;
}
