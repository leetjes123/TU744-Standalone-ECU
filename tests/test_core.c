#include "control.h"
#include "oem.h"
#include "oem_history.h"
#include "diagnostic_monitors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern u8 fake_eeprom[8192], fake_injectors[4], fake_coils[2], fake_iac_ok, fake_iac_response;
extern u16 fake_irq, fake_injector_ticks[4];
extern s32 fake_write_budget;
extern u32 fake_watchdog_services;
extern u8 fake_iac_command, fake_fan, fake_heaters;
extern u8 fake_eeprom_status, fake_eeprom_read_fail;
extern u8 fake_hold_fault, fake_hold_active, fake_hold_high;
extern u32 fake_iac_transfers;
static unsigned tests;
extern unsigned test_lifecycle(void);
extern unsigned test_faults(void);
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        tests++;                                                                                   \
        assert(x);                                                                                 \
    } while (0)
static void setup(void) {
    ecu_init(1);
    ecu.key_input = 1; /* Explicit test fixture; target now waits for P4.4. */
    cal_example(ecu.cal.bytes[0]);
    ecu.cal.valid = 1;
    ecu.cal.generation = 1;
    fake_iac_ok = 1;
    fake_iac_response = 0xC0;
    fake_hold_fault = fake_hold_active = 0;
    fake_heaters = 0;
}
static void feed(const u8 *p, u16 n, u32 now) {
    u16 i;
    for (i = 0; i < n; i++)
        protocol_receive(p[i]);
    protocol_poll(now);
}
static void test_math_cal(void) {
    u16 error, i;
    setup();
    CHECK(cal_validate(cal_active(), &error));
    CHECK(us_ticks(5000) == 6250);
    CHECK(us_ticks(25000) == 31250);
    CHECK(scale32(0xFFFFFFFFUL, 2, 1) == 0xFFFFFFFFUL);
    CHECK(lerp(255, 0, 256) == 0);
    CHECK(cal_begin());
    ecu.cal.bytes[1][0x401] = 1;
    CHECK(cal_commit(1));
    CHECK(ecu.cal.generation == 2);
    CHECK(cal_begin());
    put16(ecu.cal.bytes[0] + 0x7B2, 0);
    CHECK(!cal_commit(1));
    CHECK(ecu.cal.active == 1);
    cal_abort();
    CHECK(cal_begin());
    put16(ecu.cal.bytes[0] + CAL_IAC_MAX, 200);
    CHECK(!cal_commit(0));
    cal_abort();
    for (i = 0; i < 16; i++) {
        setup();
        put16(ecu.cal.bytes[0] + 0x400 + 2 * i, 65535);
        CHECK(!cal_validate(cal_active(), &error));
    }
    /* Running edits retain the pre-optimization protected-byte contract.
       Validate each mutation first to distinguish schema rejection from
       structural rejection, including both sides of every range boundary. */
    for (i = 0; i < CAL_SIZE; i++) {
        u8 protected_byte = (u8)((i >= 0x460 && i < 0x480) ||
            (i >= 0x550 && i < 0x5B4) || i == 0x5D4 || i == 0x5E4 ||
            (i >= 0x600 && i < 0x603) || (i >= 0x605 && i < 0x607) ||
            (i >= 0x7A1 && i < 0x7AF) || (i >= 0x7B0 && i < 0x7B4) ||
            (i >= 0x900 && (i < CAL_DFCO_EXIT_RPM || i >= CAL_DFCO_EXIT_RPM + 2U)));
        setup(); CHECK(cal_begin()); ecu.cal.bytes[1][i] ^= 1;
        if (!cal_validate(ecu.cal.bytes[1], &error)) continue;
        CHECK(cal_commit(0) == !protected_byte);
        CHECK(ecu.cal.active == !protected_byte);
        if (protected_byte) CHECK(ecu.cal.error_offset == i);
    }
}
static void test_rotation(void) {
    u16 rev, tooth;
    u32 stamp = 0;
    setup();
    rotation_edge(stamp);
    for (rev = 0; rev < 4; rev++) {
        for (tooth = 0; tooth < 57; tooth++) {
            stamp += 1250;
            rotation_edge(stamp);
        }
        stamp += 3750;
        rotation_edge(stamp);
    }
    CHECK(ecu.rotation.state == ROT_VALID);
    CHECK(ecu.rotation.rpm == 1000);
    CHECK(ecu.rotation.tooth == 0);
    stamp += 2500;
    rotation_edge(stamp);
    CHECK(ecu.rotation.state == ROT_UNSYNCED);
    CHECK(ecu.authority.inhibits & INH_SYNC);
}
/* rotation_block must equal the original XP1 decode: every edge through
   rotation_edge in order, a rejected edge revoking synchronization, and the
   smallest accepted interval. Randomized 60-2 input with jitter, noise,
   extra/missing teeth, stalls, 16-bit wraps and slow/fast rotation, split into
   random 1..30-edge blocks; misfire eligibility is fixed per block. States are
   compared after every block. */
typedef struct {
    Rotation rot;
    MisfireObservation mis;
    u16 inhibits, epoch;
    u32 faults, fastest;
    u8 accepted;
} BlockState;
static void block_reference(const u32 *stamps, u8 n, u32 *fastest, u8 *accepted) {
    u8 i, valid;
    u32 interval;
    *fastest = 0xFFFFFFFFUL;
    *accepted = 0;
    for (i = 0; i < n; i++) {
        interval = stamps[i] - ecu.rotation.last;
        if (interval >= 100UL && interval < *fastest) *fastest = interval;
        valid = rotation_edge(stamps[i]);
        *accepted |= valid;
        if (!valid && ecu.rotation.state == ROT_VALID) {
            ecu.rotation.state = ROT_UNSYNCED; ecu.rotation.have_gap = 0;
            ecu.rotation.epoch++; ecu.rotation.losses++;
            safety_inhibit(INH_SYNC, ecu.milliseconds);
        }
    }
}
static u16 block_run(u32 seed, u8 candidate, BlockState *out, u16 max_out) {
    u32 stamp = 1000, r = seed, stamps[30];
    u16 caps[30], normal = 1250, slot = 0, blocks = 0, edges = 0;
    u8 n, i;
    setup();
    memset(&misfire_observation, 0, sizeof(misfire_observation));
    ecu.authority.inhibits = 0;
    while (blocks < max_out) {
        r = r * 1103515245UL + 12345UL;
        n = (u8)(1U + ((r >> 16) % 30U));
        ecu.control.mode = ((r >> 8) & 7U) ? ENGINE_RUNNING : ENGINE_CRANKING;
        for (i = 0; i < n; i++) {
            u32 dt;
            r = r * 1103515245UL + 12345UL;
            if (((r >> 16) & 1023U) == 7U) normal = (u16)(80U + ((r >> 8) & 8191U));
            dt = slot ? normal : 3UL * normal;
            switch ((r >> 24) & 255U) {
            case 0: dt = dt / 3U; break;
            case 1: dt = dt * 2U; break;
            case 2: dt = dt + (dt >> 1); break;
            case 3: dt = dt - (dt >> 2); break;
            case 4: if (((r >> 8) & 63U) == 0U) dt = 300000UL; break;
            default: dt = dt + ((r >> 4) & 15U) - 8U; break;
            }
            stamp += dt;
            slot = (u16)((slot + 1U) % 58U);
            caps[i] = (u16)stamp;
            stamps[i] = stamp;
        }
        /* The XP1 worker reconstructs earlier stamps from 16-bit differences. */
        for (i = (u8)(n - 1U); i; i--)
            stamps[i - 1U] = stamps[i] - (u16)(caps[i] - caps[i - 1U]);
        if (candidate)
            out[blocks].accepted = rotation_block(caps, n, stamp, diagnostic_edge_eligible(), &out[blocks].fastest);
        else
            block_reference(stamps, n, &out[blocks].fastest, &out[blocks].accepted);
        out[blocks].rot = ecu.rotation;
        out[blocks].mis = misfire_observation;
        out[blocks].inhibits = ecu.authority.inhibits;
        out[blocks].epoch = ecu.authority.epoch;
        out[blocks].faults = ecu.authority.faults;
        edges += n;
        blocks++;
    }
    return edges;
}
static void test_rotation_block(void) {
    static BlockState a[400], b[400];
    u32 seed;
    u16 i, fast_blocks = 0;
    for (seed = 1; seed <= 60; seed++) {
        block_run(seed * 7919UL, 0, a, 400);
        block_run(seed * 7919UL, 1, b, 400);
        for (i = 0; i < 400; i++) {
            CHECK(!memcmp(&a[i].rot, &b[i].rot, sizeof(Rotation)));
            CHECK(!memcmp(&a[i].mis, &b[i].mis, sizeof(MisfireObservation)));
            CHECK(a[i].inhibits == b[i].inhibits && a[i].epoch == b[i].epoch && a[i].faults == b[i].faults);
            CHECK(a[i].fastest == b[i].fastest && a[i].accepted == b[i].accepted);
            if (b[i].rot.state == ROT_VALID) fast_blocks++;
        }
    }
    CHECK(fast_blocks > 1000U); /* synchronized blocks, where the fast path applies */
}
/* Table math must be bit-identical to the original implementations. */
static s16 ref_lerp(s16 a, s16 b, u16 f) {
    return (s16)((s32)a + ((s32)b - a) * f / 256L);
}
static u16 ref_axis(const u8 *axis, u8 n, s16 x, u8 signed_axis, u8 *index) {
    u8 i;
    s32 a, b;
    a = signed_axis ? (s32)(s16)get16(axis) : (s32)get16(axis);
    if (x <= a) { *index = 0; return 0; }
    for (i = 0; i < n - 1; i++) {
        a = signed_axis ? (s32)(s16)get16(axis + 2U * i) : (s32)get16(axis + 2U * i);
        b = signed_axis ? (s32)(s16)get16(axis + 2U * (i + 1U)) : (s32)get16(axis + 2U * (i + 1U));
        if (x < b) { *index = i; return b > a ? (u16)(((s32)x - a) * 256L / (b - a)) : 0; }
    }
    *index = (u8)(n - 2);
    return 256;
}
static u16 ref_table1(const u8 *c, u16 offset, s16 t, u8 wide) {
    u8 i; u16 f; s16 a, b;
    f = ref_axis(c + 0x460, 16, t, 1, &i);
    a = wide ? (s16)get16(c + offset + 2U * i) : c[offset + i];
    b = wide ? (s16)get16(c + offset + 2U * (i + 1U)) : c[offset + i + 1U];
    return (u16)ref_lerp(a, b, f);
}
static u8 ref_table2(const u8 *c, u16 offset, u16 rpm, u16 load, u16 load_axis) {
    u8 x, y; u16 fx, fy; s16 a, b;
    fx = ref_axis(c + 0x400, 16, (s16)rpm, 0, &x);
    fy = ref_axis(c + load_axis, 16, (s16)load, 0, &y);
    a = ref_lerp(c[offset + 16U * y + x], c[offset + 16U * y + x + 1U], fx);
    b = ref_lerp(c[offset + 16U * (y + 1U) + x], c[offset + 16U * (y + 1U) + x + 1U], fx);
    return (u8)ref_lerp(a, b, fy);
}
static void test_table_math(void) {
    static u8 c[CAL_SIZE];
    u32 r = 12345, trial, i;
    s32 a, b;
    u16 f;
    u8 x1, x2;
    for (a = -32768L; a <= 32767L; a += 257L)
        for (b = -32768L; b <= 32767L; b += 263L)
            for (f = 0; f <= 256U; f = (u16)(f + 13U))
                CHECK(lerp((s16)a, (s16)b, f) == ref_lerp((s16)a, (s16)b, f));
    for (trial = 0; trial < 4000UL; trial++) {
        u8 mono = (u8)(trial & 1U);
        u16 v = 0;
        for (i = 0; i < CAL_SIZE; i++) { r = r * 1103515245UL + 12345UL; c[i] = (u8)(r >> 16); }
        if (mono) {
            /* Realistic increasing axes; odd trials keep random (possibly unordered) ones. */
            for (i = 0; i < 16U; i++) {
                r = r * 1103515245UL + 12345UL; v = (u16)(v + ((r >> 16) & 1023U));
                put16(c + 0x400 + 2U * i, v); put16(c + 0x420 + 2U * i, (u16)(v / 16U));
                put16(c + 0x460 + 2U * i, (u16)(s16)((s32)v / 4 - 2000));
            }
        }
        for (i = 0; i < 40U; i++) {
            s16 t; u16 rpm, load;
            r = r * 1103515245UL + 12345UL; t = (s16)(r >> 16);
            r = r * 1103515245UL + 12345UL; rpm = (u16)((r >> 16) & 0x7FFFU);
            r = r * 1103515245UL + 12345UL; load = (u16)((r >> 16) & 0x7FFFU);
            CHECK(axis_fraction(c + 0x460, 16, t, 1, &x1) == ref_axis(c + 0x460, 16, t, 1, &x2) && x1 == x2);
            CHECK(axis_fraction(c + 0x400, 16, (s16)rpm, 0, &x1) == ref_axis(c + 0x400, 16, (s16)rpm, 0, &x2) && x1 == x2);
            CHECK(table1(c, 0x490, t, 1) == ref_table1(c, 0x490, t, 1));
            CHECK(table1(c, 0x480, t, 0) == ref_table1(c, 0x480, t, 0));
            CHECK(table2(c, 0x100, rpm, load, 0x420) == ref_table2(c, 0x100, rpm, load, 0x420));
            CHECK(table2(c, 0x200, rpm, load, 0x440) == ref_table2(c, 0x200, rpm, load, 0x440));
        }
    }
}
static void test_authority(void) {
    EnginePlan p;
    setup();
    memset(&p, 0, sizeof(p));
    ecu.rotation.state = ROT_VALID;
    ecu.authority.inhibits = 0;
    p.epoch = ecu.authority.epoch;
    p.pulse_us = 5000;
    p.dwell_us = 3000;
    CHECK(safety_publish(&p));
    CHECK(injector_admit(0));
    CHECK(fake_injector_ticks[0] == 6250);
    CHECK(!injector_admit(0));
    CHECK(coil_admit(0, p.epoch, 3000));
    safety_inhibit(INH_SYNC, 0);
    CHECK(!fake_injectors[0] && fake_coils[0] && ecu.authority.spark_draining == 1);
    coil_done(0);
    CHECK(!fake_coils[0] && !ecu.authority.spark_draining);
    CHECK(!safety_publish(&p));
    CHECK(!coil_admit(0, p.epoch, 3000));
    CHECK(fake_irq == 1);
    fake_irq = 0;
    safety_inhibit(INH_SYNC, 0);
    CHECK(fake_irq == 0);
    fake_irq = 1;
    ecu.milliseconds = 100;
    p.epoch = ecu.authority.epoch;
    ecu.authority.inhibits = 0;
    safety_publish(&p);
    CHECK(!injector_admit(0));
}
static void test_parser(void) {
    static const u8 malformed[] = {0xAA, 4, 5, 0, 0x20, 0x20, 0x49};
    u8 old[32];
    u16 i;
    setup();
    memcpy(old, cal_active() + 32, 32);
    feed(malformed, sizeof(malformed), 0);
    CHECK(!memcmp(old, cal_active() + 32, 32));
    CHECK(ecu.protocol.rejected == 1);
    CHECK(!ecu.cal.staging);
    setup();
    protocol_receive(0xAA);
    protocol_poll(0);
    protocol_poll(101);
    CHECK(ecu.protocol.state == 0);
    for (i = 0; i < 200; i++)
        protocol_receive((u8)i);
    protocol_poll(102);
    CHECK(ecu.protocol.dropped == 1);
}
static void test_iac(void) {
    u32 now;
    setup();
    iac_home(0);
    CHECK(ecu.iac.state == IAC_HOMING);
    iac_service(4000);
    CHECK(ecu.iac.state == IAC_FAULT);
    CHECK(ecu.iac.fault == 5);
    setup();
    iac_home(0);
    for (now = 0; now <= 2500; now += 5)
        iac_service(now);
    CHECK(ecu.iac.state == IAC_READY);
    CHECK(ecu.iac.position == 0);
    iac_disable();
    CHECK(ecu.iac.state == IAC_UNKNOWN);
    /* Calibrated homing travel: zero keeps 250; the count must cover the
       maximum position and fit the home timeout at 12 ms per step. */
    {
        u16 error;
        setup();
        CHECK(get16(cal_active() + CAL_IAC_HOME_STEPS) == 0);
        put16(ecu.cal.bytes[0] + CAL_IAC_HOME_STEPS, 300);
        CHECK(cal_validate(cal_active(), &error));
        iac_home(0);
        CHECK(ecu.iac.remaining == 300);
        for (now = 0; now < 2995; now += 5)
            iac_service(now);
        CHECK(ecu.iac.state == IAC_HOMING && ecu.iac.remaining == 1);
        iac_service(2995);
        CHECK(ecu.iac.state == IAC_READY && ecu.iac.position == 0);
        put16(ecu.cal.bytes[0] + CAL_IAC_HOME_STEPS, 179); /* below IAC maximum 180 */
        CHECK(!cal_validate(cal_active(), &error) && error == CAL_IAC_HOME_STEPS);
        put16(ecu.cal.bytes[0] + CAL_IAC_HOME_STEPS, 180);
        CHECK(cal_validate(cal_active(), &error));
        put16(ecu.cal.bytes[0] + CAL_IAC_HOME_STEPS, 801);
        CHECK(!cal_validate(cal_active(), &error) && error == CAL_IAC_HOME_STEPS);
        put16(ecu.cal.bytes[0] + CAL_IAC_HOME_STEPS, 334); /* 4008 ms > 4000 ms timeout */
        CHECK(!cal_validate(cal_active(), &error) && error == CAL_IAC_HOME_STEPS);
        put16(ecu.cal.bytes[0] + CAL_IAC_HOME_STEPS, 333);
        CHECK(cal_validate(cal_active(), &error));
        put16(ecu.cal.bytes[0] + CAL_IAC_HOME_STEPS, 0);
        put16(ecu.cal.bytes[0] + CAL_HOME_MS, 3000); /* default 250 x 12 fits exactly */
        CHECK(cal_validate(cal_active(), &error));
    }
    setup();
    iac_home(0);
    fake_iac_ok = 0;
    iac_service(0);
    CHECK(ecu.iac.state == IAC_FAULT);
}
static void test_iac_direction_and_status(void) {
    u32 now, transfers;
    u8 i;
    static const u8 closing[4] = {0x13, 0x1B, 0x1A, 0x12};
    setup();
    ecu.iac.position = 72;
    iac_home(0);
    CHECK(ecu.iac.position == 72); /* No fabricated zero before reference. */
    for (i = 0; i < 4; i++) {
        iac_service((u32)i * 10UL);
        CHECK(fake_iac_command == closing[i]);
        CHECK(ecu.iac.remaining == 250U - i);
        iac_service((u32)i * 10UL + 5UL);
        CHECK(ecu.iac.remaining == 249U - i);
    }
    iac_service(4000);
    CHECK(ecu.iac.fault == 5 && ecu.iac.position == 72);
    setup();
    ecu.iac.state = IAC_READY;
    ecu.iac.target = 1;
    iac_service(0);
    CHECK(fake_iac_command == 0x1A); /* Opening reverses table traversal. */
    CHECK(ecu.iac.position == 0 && ecu.iac.pending == 1);
    iac_service(5);
    CHECK(ecu.iac.position == 1);
    CHECK(ecu.iac.holding && fake_hold_active && fake_hold_high == 0x1A);
    ecu.iac.target = 0;
    iac_service(10);
    CHECK(fake_iac_command == 0x12);
    iac_service(15);
    CHECK(ecu.iac.position == 0);

    setup();
    iac_home(0);
    fake_iac_response = 0x40;
    for (now = 0; now < 85; now += 5) {
        iac_service(now);
        CHECK(ecu.iac.state == IAC_HOMING);
    }
    CHECK(ecu.iac.open_count == 8);
    iac_service(85); /* Ninth qualified drive response; hold samples ignored. */
    CHECK(ecu.iac.state == IAC_FAULT && ecu.iac.fault == 2);
    CHECK(fake_iac_command == 0x3F);
    CHECK(ecu.iac.remaining == 0 && !ecu.iac.pending);

    setup();
    iac_home(0);
    iac_service(0);
    fake_iac_response = 0x40;
    iac_service(5);
    CHECK(ecu.iac.open_count == 1);
    fake_iac_response = 0xC0;
    iac_service(10); /* Hold response cannot certify recovery. */
    CHECK(ecu.iac.open_count == 1);
    iac_service(15);
    CHECK(ecu.iac.open_count == 0);
    fake_iac_response = 0x80;
    iac_service(20); /* Short indication is actionable even in hold. */
    CHECK(ecu.iac.fault == 3);

    setup();
    fake_iac_ok = 0;
    CHECK(!service_enter(0));
    CHECK(ecu.service && ecu.iac.off_pending && ecu.iac.fault == 6);
    transfers = fake_iac_transfers;
    iac_service(0);
    CHECK(fake_iac_transfers == transfers + 1);
    iac_service(1);
    CHECK(fake_iac_transfers == transfers + 1);
    fake_iac_ok = 1;
    iac_service(10);
    CHECK(!ecu.iac.off_pending && ecu.iac.fault == 6);
    CHECK(ecu.iac.state == IAC_FAULT && fake_iac_command == 0x3F);
    setup();
    ecu.iac.state = IAC_READY;
    ecu.iac.holding = 1;
    fake_hold_fault = 7;
    iac_service(0);
    CHECK(ecu.iac.fault == 7 && ecu.iac.state == IAC_FAULT);
    CHECK(!ecu.iac.holding && fake_iac_command == 0x3F);
}
static void fan_pass(u32 now, Rotation *r) {
    const u8 *c = cal_active();
    engine_state_update(now, r, c);
    fan_update(c);
    idle_update(now, 10, r, c);
    auxiliary_update(now, r, c);
}
static void setup_fan(Rotation *r) {
    setup();
    memset(r, 0, sizeof(*r));
    r->state = ROT_VALID;
    r->rpm = 950;
    ecu.control.key_on = 1;
    ecu.control.mode = ENGINE_RUNNING;
    ecu.control.idle_position = 30;
    ecu.sensors.clt.quality = ecu.sensors.tps.quality = QUALITY_VALID;
    ecu.sensors.clt.value = 90;
    ecu.iac.state = IAC_READY;
    ecu.iac.position = 30;
    fan_pass(10000, r);
    CHECK(!fake_fan && ecu.iac.target == 30);
    ecu.sensors.clt.value = 95;
}
static void test_fan_preload(void) {
    Rotation r;
    setup_fan(&r);
    fan_pass(10010, &r);
    CHECK(ecu.control.fan_request && !fake_fan);
    CHECK(ecu.control.fan_waiting && ecu.iac.target == 34);
    ecu.iac.position = 33;
    fan_pass(10020, &r);
    CHECK(!fake_fan);
    ecu.iac.position = 34;
    ecu.iac.pending = 1;
    fan_pass(10030, &r);
    CHECK(!fake_fan);
    ecu.iac.pending = 0;
    fan_pass(10040, &r);
    CHECK(fake_fan && !ecu.control.fan_waiting);
    ecu.sensors.clt.value = 92;
    fan_pass(10050, &r);
    CHECK(fake_fan); /* Demand hysteresis survives relay sequencing. */
    ecu.sensors.clt.value = 90;
    fan_pass(10060, &r);
    CHECK(!fake_fan && !ecu.control.fan_request && ecu.iac.target == 30);

    setup_fan(&r);
    fan_pass(0xFFFFFF00UL, &r);
    fan_pass(0x000000F3UL, &r);
    CHECK(!fake_fan);
    fan_pass(0x000000F4UL, &r); /* 500 ms timeout across u32 wrap. */
    CHECK(fake_fan);

    setup_fan(&r);
    fan_pass(10010, &r);
    ecu.sensors.clt.value = 90;
    fan_pass(10020, &r);
    CHECK(!fake_fan && !ecu.control.fan_waiting);
    ecu.sensors.clt.quality = QUALITY_STALE;
    fan_pass(10030, &r);
    CHECK(fake_fan); /* Failed coolant input bypasses preload delay. */

    setup_fan(&r);
    fan_pass(10010, &r);
    ecu.iac.state = IAC_FAULT;
    fan_pass(10020, &r);
    CHECK(fake_fan);

    setup_fan(&r);
    ecu.cal.generation++; /* Feedforward survives bumpless gain reseeding. */
    fan_pass(10010, &r);
    CHECK(ecu.iac.target == 34 && !fake_fan);
    r.rpm = 1000;
    fan_pass(10020, &r);
    CHECK(ecu.iac.target >= 34 && !fake_fan);
    put16(ecu.cal.bytes[0] + CAL_IAC_MAX, 30);
    fan_pass(10030, &r);
    CHECK(fake_fan && ecu.iac.target == 30);
}
static void test_fuel_percentages(void) {
    u8 *c;
    u16 i, error;
    Rotation r;
    EnginePlan p;
    setup();
    c = ecu.cal.bytes[0];
    memset(&r, 0, sizeof(r));
    memset(&p, 0, sizeof(p));
    r.rpm = p.rpm = 1000;
    ecu.sensors.iat.value = 0;
    ecu.sensors.map.value = 100;
    ecu.sensors.clt.value = 20;
    ecu.sensors.battery.value = 12000;
    ecu.control.mode = ENGINE_CRANKING;
    for (i = 0; i < 16; i++) put16(c + 0x490 + 2 * i, 80);
    fuel_plan(0, &r, c, &p);
    CHECK(ecu.control.ve == 80 && p.requested_us == 2850); /* 5000*.8/2 + 850 */
    ecu.sensors.map.value = 50;
    fuel_plan(0, &r, c, &p);
    CHECK(p.requested_us == 1850);
    c[0x5D4] = CFG_ALPHA_N;
    fuel_plan(0, &r, c, &p);
    CHECK(p.requested_us == 2850);
    put16(c + 0x5DF, 10000);
    fuel_plan(0, &r, c, &p);
    CHECK(p.requested_us == 4850);
    put16(c + 0x5DF, 5000);
    for (i = 0; i < 16; i++) put16(c + 0x490 + 2 * i, 400);
    fuel_plan(0, &r, c, &p);
    CHECK(ecu.control.ve == 400 && p.requested_us == 10850);
    for (i = 0; i < 16; i++) put16(c + 0x490 + 2 * i, 0);
    fuel_plan(0, &r, c, &p);
    CHECK(p.requested_us == 0 && p.pulse_us == 0); /* No dead time on zero fuel. */
    for (i = 0; i < 16; i++) {
        put16(c + 0x490 + 2 * i, 80);
        c[0x4B0 + i] = 150;
        c[0x4C0 + i] = 10;
    }
    memset(c, 100, 256);
    ecu.control.mode = ENGINE_RUNNING;
    ecu.control.running_at = 0xFFFFFF00UL;
    fuel_plan(0xFFFFFF00UL, &r, c, &p);
    CHECK(ecu.control.afterstart == 150 && p.requested_us == 4600);
    fuel_plan(244, &r, c, &p); /* Halfway, across clock wrap. */
    CHECK(ecu.control.afterstart == 125 && p.requested_us == 3975);
    fuel_plan(744, &r, c, &p);
    CHECK(ecu.control.afterstart == 100 && p.requested_us == 3350);
    memset(c + 0x4C0, 0, 16);
    fuel_plan(ecu.control.running_at, &r, c, &p);
    CHECK(ecu.control.afterstart == 100 && p.requested_us == 3350);
    ecu.control.ae_percent = 150;
    fuel_plan(744, &r, c, &p);
    CHECK(p.requested_us == 4600); /* Multiplies fuel; dead time stays 850. */
    put16(c + 0x5DF, 10000);
    fuel_plan(744, &r, c, &p);
    CHECK(p.requested_us == 8350); /* AE scales with base fuel. */
    put16(c + 0x5DF, 5000);
    memset(c + 0x4C0, 10, 16);
    fuel_plan(ecu.control.running_at, &r, c, &p);
    CHECK(p.requested_us == 6475); /* 2500 * 1.5 * 1.5 + 850. */
    ecu.control.mode = ENGINE_CRANKING;
    fuel_plan(ecu.control.running_at, &r, c, &p);
    CHECK(p.requested_us == 2850); /* No running enrichments while cranking. */

    ecu.control.mode = ENGINE_RUNNING;
    ecu.sensors.tps.quality = QUALITY_VALID;
    ecu.sensors.tps.value = 0;
    ecu.control.ae_percent = ecu.control.ae_peak = 100;
    memset(c + 0x759, 200, 48);
    memset(c + 0x799, 50, 8);
    c[0x758] = 1;
    acceleration_update(0xFFFFFF00UL, 10, 1000, c);
    ecu.sensors.tps.value = 100;
    acceleration_update(0xFFFFFF0AUL, 10, 1000, c);
    CHECK(ecu.control.ae_percent == 150); /* RPM scales only excess above 100. */
    acceleration_update(4, 10, 1000, c);
    CHECK(ecu.control.ae_percent == 125);
    acceleration_update(254, 10, 1000, c);
    CHECK(ecu.control.ae_percent == 100);
    memset(c + 0x799, 0, 8);
    ecu.sensors.tps.value = 200;
    acceleration_update(264, 10, 1000, c);
    CHECK(ecu.control.ae_percent == 100);
    memset(c + 0x799, 255, 8);
    memset(c + 0x759, 255, 48);
    ecu.sensors.tps.value = 300;
    acceleration_update(274, 10, 1000, c);
    CHECK(ecu.control.ae_percent == 495);
    ecu.sensors.tps.quality = QUALITY_RANGE;
    acceleration_update(284, 10, 1000, c);
    CHECK(ecu.control.ae_percent == 100 && ecu.control.ae_peak == 100);
    ecu.sensors.tps.quality = QUALITY_VALID;
    acceleration_update(294, 10, 1000, c);
    acceleration_update(304, 10, 1000, c);
    CHECK(ecu.control.ae_percent == 100); /* Old enrichment cannot resurrect. */

    CHECK(cal_validate(c, &error));
    put16(c + CAL_MAGIC + 2, 3);
    CHECK(!cal_validate(c, &error) && error == CAL_MAGIC);
    put16(c + CAL_MAGIC + 2, CAL_SCHEMA);
    put16(c + 0x490, 1001);
    CHECK(!cal_validate(c, &error));
    put16(c + 0x490, 1000);
    c[0x4B0] = 99;
    CHECK(!cal_validate(c, &error));
    c[0x4B0] = 100;
    c[0x759] = 99;
    CHECK(!cal_validate(c, &error));
    c[0x759] = 100;
    CHECK(cal_validate(c, &error));
}
static void test_controls(void) {
    u8 *c;
    Rotation r;
    EnginePlan p;
    u32 now;
    setup();
    c = ecu.cal.bytes[0];
    memset(&r, 0, sizeof(r));
    r.state = ROT_VALID;
    r.rpm = 700;
    ecu.control.key_on = 1;
    engine_state_update(0, &r, c);
    CHECK(ecu.control.mode == ENGINE_CRANKING);
    engine_state_update(299, &r, c);
    CHECK(ecu.control.mode == ENGINE_CRANKING);
    engine_state_update(300, &r, c);
    CHECK(ecu.control.mode == ENGINE_RUNNING);

    /* In wideband mode the upstream heater output powers the Spartan
       controller across the owner's OEM 12 V/ground wiring. This is independent
       of whether analog-only closed-loop qualification is enabled. */
    c[0x600] = 1;
    c[CAL_FLAGS] |= EQUIP_UPSTREAM_RELAY_HEATER;
    safety_conditions(INH_SYNC, 300); /* valid tune; key-on power does not require sync */
    auxiliary_update(300, &r, c);
    CHECK(fake_heaters & 1);
    ecu.service = 1;
    auxiliary_update(300, &r, c);
    CHECK(!(fake_heaters & 1));
    ecu.service = 0;
    c[0x600] = 0;

    r.rpm = 400;
    engine_state_update(301, &r, c);
    CHECK(ecu.control.mode == ENGINE_CRANKING);
    ecu.control.mode = ENGINE_RUNNING;
    ecu.sensors.tps.quality = QUALITY_VALID;
    ecu.sensors.tps.value = 200;
    acceleration_update(10, 10, 700, c);
    acceleration_update(20, 10, 700, c);
    CHECK(ecu.control.ae_percent == 100);
    CHECK(ecu.control.tps_rate == 0);
    memset(&p, 0, sizeof(p));
    p.rpm = 1000;
    ecu.control.target_afr = 147;
    ecu.control.mode = ENGINE_RUNNING;
    ecu.control.running_at = 0;
    c[0x5D4] = CFG_STFT;
    c[0x600] = 1;
    c[0x8C7] = 0;
    ecu.sensors.wideband_ready = 1;
    ecu.sensors.oxygen.quality = QUALITY_VALID;
    ecu.sensors.clt.quality = QUALITY_VALID;
    ecu.sensors.clt.value = 90;
    ecu.sensors.tps.quality = QUALITY_VALID;
    put16(c + CAL_STFT_KI, 400);
    c[0x731] = 192;
    for (now = 500; now < 60000; now += 500) {
        ecu.sensors.afr10 = (u16)(176400UL / ecu.control.applied_trim * 1024UL / 1000UL);
        lambda_update(now, c, &p);
    }
    CHECK(ecu.control.applied_trim > 1170 && ecu.control.applied_trim < 1280);
    ecu.sensors.oxygen.quality = QUALITY_STALE;
    lambda_update(now, c, &p);
    CHECK(ecu.control.applied_trim == 1024 && !ecu.control.trim_enabled);
    ecu.control.trim_q16 = 90000;
    c[0x731] = 128;
    lambda_update(now + 1, c, &p);
    CHECK(ecu.control.trim_q16 == 65536);
}
static void test_storage(void) {
    u32 now, cut;
    u8 baseline[8192];
    setup();
    memset(fake_eeprom, 0xFF, 8192);
    CHECK(storage_save(0));
    for (now = 0; ecu.storage.phase && now < 1000; now++)
        storage_poll(now);
    CHECK(ecu.storage.result == 1);
    memcpy(baseline, fake_eeprom, 8192);
    for (cut = 0; cut < 205; cut++) {
        memcpy(fake_eeprom, baseline, 8192);
        ecu_init(1);
        storage_load();
        CHECK(ecu.cal.valid);
        CHECK(cal_active()[0] == 60);
        ecu.cal.bytes[ecu.cal.active][0] = 70;
        CHECK(storage_save(0));
        for (now = 0; now < cut; now++)
            storage_poll(now);
        ecu_init(1);
        storage_load();
        CHECK(ecu.cal.valid);
        CHECK(cal_active()[0] == 60 || cal_active()[0] == 70);
        CHECK(cal_validate(cal_active(), &ecu.cal.error_offset));
    }
    /* Tear each possible byte of invalidate, payload, header and commit writes.
       Reset immediately; never rely on the writer observing an error. */
    for (cut = 0; cut <= 3106; cut++) {
        memcpy(fake_eeprom, baseline, 8192);
        ecu_init(1);
        storage_load();
        ecu.cal.bytes[ecu.cal.active][0] = 70;
        CHECK(storage_save(0));
        fake_write_budget = (s32)cut;
        for (now = 0; ecu.storage.phase && fake_write_budget && now < 300; now++)
            storage_poll(now);
        fake_write_budget = -1;
        ecu_init(1);
        storage_load();
        CHECK(ecu.cal.valid);
        CHECK(cal_active()[0] == 60 || cal_active()[0] == 70);
    }
}
static void test_history_storage(void) {
    u8 baseline[8192], old_payload[OEM_HISTORY_SIZE], new_payload[OEM_HISTORY_SIZE];
    OemHistory h;
    OemDiagnostics state, restored;
    u32 now, cut;
    u16 i;
    setup();
    memset(&h, 0, sizeof(h));
    memset(&state, 0, sizeof(state));
    memset(fake_eeprom, 0xFF, sizeof(fake_eeprom));
    CHECK(storage_save(0));
    for (now = 0; ecu.storage.phase && now < 300; now++)
        storage_poll(now);
    CHECK(ecu.storage.result == 1);
    state.events.live[0x61] = 0x2403;
    state.events.timestamp = 42;
    CHECK(oem_history_encode(&state, old_payload));
    CHECK(oem_history_save(&h, &state, 0));
    for (now = 0; h.phase && now < 200; now++)
        oem_history_poll(&h, now);
    CHECK(h.result == OEM_HISTORY_OK && h.valid && h.sequence == 1);
    memcpy(baseline, fake_eeprom, sizeof(baseline));
    state.events.timestamp = 99;
    state.events.live[0x61] = 0x2002;
    CHECK(oem_history_encode(&state, new_payload));
    /* Tear every byte in invalidate/payload/header/commit. A reset may see
       only the previous complete snapshot or the new complete snapshot. */
    for (cut = 0; cut <= OEM_HISTORY_SIZE + 34UL; cut++) {
        memcpy(fake_eeprom, baseline, sizeof(baseline));
        setup();
        memset(&h, 0, sizeof(h));
        memset(&restored, 0, sizeof(restored));
        CHECK(oem_history_load(&h, &restored));
        CHECK(restored.events.timestamp == 42);
        CHECK(oem_history_save(&h, &state, 0));
        fake_write_budget = (s32)cut;
        for (now = 0; h.phase && fake_write_budget && now < 200; now++)
            oem_history_poll(&h, now);
        fake_write_budget = -1;
        setup(); /* Simulated reset also discards the foreground EEPROM lease. */
        memset(&h, 0, sizeof(h));
        CHECK(oem_history_load(&h, &restored));
        CHECK(oem_history_encode(&restored, h.payload));
        CHECK(!memcmp(h.payload, old_payload, OEM_HISTORY_SIZE) ||
              !memcmp(h.payload, new_payload, OEM_HISTORY_SIZE));
        CHECK(!memcmp(fake_eeprom, baseline, OEM_HISTORY_SLOT0));
        CHECK(!memcmp(fake_eeprom + 4096, baseline + 4096, OEM_HISTORY_SLOT0));
        storage_load();
        CHECK(ecu.cal.valid && cal_active()[0] == 60);
    }
    /* The snapshot is immutable while queued, even if live history changes. */
    memset(&h, 0, sizeof(h));
    CHECK(oem_history_load(&h, &restored));
    CHECK(oem_history_save(&h, &state, 0));
    state.events.timestamp = 1234;
    for (now = 0; h.phase && now < 200; now++)
        oem_history_poll(&h, now);
    CHECK(oem_history_load(&h, &restored));
    CHECK(restored.events.timestamp == 99);
    /* Repeated calibration saves must not erase either history journal. */
    memcpy(baseline, fake_eeprom, sizeof(baseline));
    for (i = 0; i < 2; i++) {
        CHECK(storage_save(0));
        for (now = 0; ecu.storage.phase && now < 300; now++)
            storage_poll(now);
        CHECK(ecu.storage.result == 1);
        CHECK(!memcmp(fake_eeprom + OEM_HISTORY_SLOT0, baseline + OEM_HISTORY_SLOT0, 800));
        CHECK(!memcmp(fake_eeprom + OEM_HISTORY_SLOT1, baseline + OEM_HISTORY_SLOT1, 800));
    }
    /* Corruption of the newest committed payload falls back to the older slot. */
    fake_eeprom[(h.active_slot ? OEM_HISTORY_SLOT1 : OEM_HISTORY_SLOT0) + 50] ^= 1;
    memset(&h, 0, sizeof(h));
    CHECK(oem_history_load(&h, &restored));
    CHECK(restored.events.timestamp == 99);
    /* Service admission and failed/expired writes never invalidate the old slot. */
    ecu.control.mode = ENGINE_RUNNING;
    CHECK(!oem_history_save(&h, &state, 0));
    ecu.control.mode = ENGINE_STOPPED;
    CHECK(oem_history_save(&h, &state, 0xFFFFFF00UL));
    oem_history_poll(&h, (u32)(0xFFFFFF00UL + 10000UL));
    CHECK(h.result == OEM_HISTORY_TIMEOUT && !h.phase);
    state.events.store.count = 21;
    CHECK(!oem_history_save(&h, &state, 0));
    CHECK(h.result == OEM_HISTORY_INVALID);
    state.events.store.count = 0;
    CHECK(oem_history_save(&h, &state, 0));
    fake_eeprom_status = 1;
    oem_history_poll(&h, 1);
    CHECK(h.phase == 1 && h.result == 0);
    fake_eeprom_status = 2;
    oem_history_poll(&h, 2);
    CHECK(h.phase == 0 && h.result == OEM_HISTORY_IO);
    fake_eeprom_status = 0;
    fake_eeprom_read_fail = 1;
    restored.events.timestamp = 777;
    CHECK(!oem_history_load(&h, &restored));
    CHECK(h.result == OEM_HISTORY_IO && restored.events.timestamp == 777);
    fake_eeprom_read_fail = 0;
    /* Serial-number comparison must select the committed zero after FFFFFFFF. */
    memset(fake_eeprom, 0xFF, sizeof(fake_eeprom));
    memset(&h, 0, sizeof(h));
    h.sequence = 0xFFFFFFFEUL;
    state.events.timestamp = 1111;
    CHECK(oem_history_save(&h, &state, 0));
    for (now = 0; h.phase && now < 200; now++)
        oem_history_poll(&h, now);
    CHECK(h.sequence == 0xFFFFFFFFUL);
    state.events.timestamp = 2222;
    CHECK(oem_history_save(&h, &state, 0));
    for (now = 0; h.phase && now < 200; now++)
        oem_history_poll(&h, now);
    CHECK(h.sequence == 0);
    memset(&h, 0, sizeof(h));
    CHECK(oem_history_load(&h, &restored));
    CHECK(h.sequence == 0 && restored.events.timestamp == 2222);
}
static void history_schema1(u16 at) {
    u8 *header = fake_eeprom + at;
    u8 *payload = header + 32;
    /* A CRC-valid development schema1 snapshot: original717 retained bytes,
       then nineteen zero padding bytes. It contains no completion state. */
    memset(payload + 749, 0, 19);
    put16(header + 4, 1);
    put16(header + 12, crc16(0xFFFFU, payload, OEM_HISTORY_SIZE));
    put16(header + 20, crc16(0xFFFFU, header, 20));
}
static void test_history_schema(void) {
    OemHistory h;
    OemDiagnostics state, restored, before;
    u32 now;
    setup();
    memset(&h, 0, sizeof(h));
    memset(&state, 0, sizeof(state));
    memset(fake_eeprom, 0xFF, sizeof(fake_eeprom));
    state.events.timestamp = 111;
    state.readiness.pending = 0x6D;
    state.readiness.count[0] = 43;
    CHECK(oem_history_save(&h, &state, 0));
    for (now = 0; h.phase && now < 200; now++)
        oem_history_poll(&h, now);
    CHECK(h.result == OEM_HISTORY_OK && h.active_slot == 0);
    state.events.timestamp = 222;
    CHECK(oem_history_save(&h, &state, 0));
    for (now = 0; h.phase && now < 200; now++)
        oem_history_poll(&h, now);
    CHECK(h.result == OEM_HISTORY_OK && h.active_slot == 1);
    /* A newer incompatible slot does not hide a valid schema2 fallback. */
    history_schema1(OEM_HISTORY_SLOT1);
    memset(&h, 0, sizeof(h));
    memset(&restored, 0, sizeof(restored));
    CHECK(oem_history_load(&h, &restored));
    CHECK(restored.events.timestamp == 111 && restored.readiness.pending == 0x6D);
    CHECK(restored.readiness.count[0] == 43 && h.active_slot == 0);
    history_schema1(OEM_HISTORY_SLOT0);
    memset(&h, 0, sizeof(h));
    before = restored;
    CHECK(!oem_history_load(&h, &restored));
    CHECK(h.result == OEM_HISTORY_INVALID && !h.valid && !ecu.storage_owner);
    CHECK(!memcmp(&before, &restored, sizeof(restored)));
}
static void test_persistence_owner(void) {
    OemHistoryOwner owner, reboot;
    OemHistory other;
    OemDiagnostics state, restored;
    u32 now, writes;
    u8 phase, action, baseline[8192];
    extern u32 fake_writes;
    extern u8 fake_write_fail;
    setup();
    memset(&owner, 0, sizeof(owner));
    memset(&state, 0, sizeof(state));
    memset(&other, 0, sizeof(other));
    memset(fake_eeprom, 0xFF, sizeof(fake_eeprom));
    CHECK(!oem_history_owner_load(&owner, &state));
    CHECK(owner.ready && owner.dirty && !ecu.storage_owner);
    CHECK(!oem_history_owner_settled(&owner));
    /* Both admission orders, a second history handle, and boot readers must
       respect the complete transaction, even between individual SSC calls. */
    CHECK(storage_save(0));
    writes = fake_writes;
    CHECK(!oem_history_owner_save(&owner, &state, 0));
    CHECK(!oem_history_load(&other, &state));
    storage_release(&other);
    CHECK(ecu.storage_owner == &ecu.storage && fake_writes == writes);
    for (now = 0; ecu.storage.phase && now < 300; now++)
        storage_poll(now);
    CHECK(ecu.storage.result == 1 && !ecu.storage_owner);
    state.events.timestamp = 42;
    CHECK(oem_history_owner_save(&owner, &state, 0));
    CHECK(!storage_save(0));
    CHECK(!oem_history_save(&other, &state, 0));
    ecu.cal.bytes[ecu.cal.active][0] = 71;
    storage_load();
    CHECK(cal_active()[0] == 71 && ecu.storage_owner == &owner.journal);
    for (now = 0; owner.journal.phase && now < 200; now++)
        oem_history_owner_poll(&owner, now);
    CHECK(oem_history_owner_settled(&owner) && !ecu.storage_owner);
    writes = fake_writes;
    CHECK(!oem_history_owner_save(&owner, &state, now));
    CHECK(fake_writes == writes);
    memcpy(baseline, fake_eeprom, sizeof(baseline));
    /* A retained mutation or a later clear at EVERY writer phase must survive
       completion of the earlier immutable snapshot as outstanding work. */
    for (action = 0; action < 2; action++) {
        for (phase = 1; phase <= 8; phase++) {
            setup();
            memcpy(fake_eeprom, baseline, sizeof(baseline));
            memset(&owner, 0, sizeof(owner));
            CHECK(oem_history_owner_load(&owner, &state));
            CHECK(state.events.timestamp == 42 && oem_history_owner_settled(&owner));
            state.events.timestamp = 99;
            oem_history_owner_cleared(&owner);
            CHECK(oem_history_owner_save(&owner, &state, 0));
            for (now = 0; owner.journal.phase != phase && now < 200; now++)
                oem_history_owner_poll(&owner, now);
            CHECK(owner.journal.phase == phase);
            state.events.timestamp = 123;
            if (action)
                oem_history_owner_cleared(&owner);
            else
                oem_history_owner_changed(&owner);
            for (; owner.journal.phase && now < 200; now++)
                oem_history_owner_poll(&owner, now);
            CHECK(owner.journal.result == OEM_HISTORY_OK && owner.dirty);
            CHECK(!oem_history_owner_settled(&owner) && !ecu.storage_owner);
            CHECK(owner.clear_pending == action && owner.clear_durable == (u8)!action);
            /* The completed snapshot really predates the later mutation. */
            memset(&other, 0, sizeof(other));
            CHECK(oem_history_load(&other, &restored));
            CHECK(restored.events.timestamp == 99);
            CHECK(oem_history_owner_save(&owner, &state, now));
            for (; owner.journal.phase && now < 400; now++)
                oem_history_owner_poll(&owner, now);
            CHECK(oem_history_owner_settled(&owner));
            CHECK(owner.clear_durable && !owner.clear_pending);
            memset(&reboot, 0, sizeof(reboot));
            CHECK(oem_history_owner_load(&reboot, &restored));
            CHECK(restored.events.timestamp == 123 && oem_history_owner_settled(&reboot));
            /* Clear completion is a session acknowledgement, never fabricated
               from a snapshot loaded after reset. */
            CHECK(!reboot.clear_durable);
        }
    }
    /* Failed writes and wraparound timeout cannot acknowledge a clear. */
    oem_history_owner_cleared(&owner);
    CHECK(oem_history_owner_save(&owner, &state, 0));
    fake_write_fail = 1;
    oem_history_owner_poll(&owner, 1);
    fake_write_fail = 0;
    CHECK(owner.journal.result == OEM_HISTORY_IO && !ecu.storage_owner);
    CHECK(owner.dirty && owner.clear_pending && !owner.clear_durable);
    CHECK(oem_history_owner_save(&owner, &state, 0xFFFFFF00UL));
    fake_eeprom_status = 1;
    oem_history_owner_poll(&owner, 0xFFFFFF01UL);
    CHECK(owner.journal.phase && ecu.storage_owner == &owner.journal);
    oem_history_owner_poll(&owner, (u32)(0xFFFFFF00UL + 10000UL));
    CHECK(owner.journal.result == OEM_HISTORY_TIMEOUT && !ecu.storage_owner);
    CHECK(owner.dirty && owner.clear_pending && !owner.clear_durable);
    fake_eeprom_status = 0;
    CHECK(oem_history_owner_save(&owner, &state, 0));
    for (now = 0; owner.journal.phase && now < 200; now++)
        oem_history_owner_poll(&owner, now);
    CHECK(oem_history_owner_settled(&owner) && owner.clear_durable);
    /* Unreadable boot history blocks replacement until a successful retry. */
    memset(&reboot, 0, sizeof(reboot));
    restored.events.timestamp = 777;
    fake_eeprom_read_fail = 1;
    CHECK(!oem_history_owner_load(&reboot, &restored));
    CHECK(!reboot.ready && restored.events.timestamp == 777 && !ecu.storage_owner);
    CHECK(!oem_history_owner_save(&reboot, &restored, 0));
    fake_eeprom_read_fail = 0;
    CHECK(oem_history_owner_load(&reboot, &restored));
    CHECK(reboot.ready && restored.events.timestamp == 123);
    /* Every failed admission releases only its own lease. */
    oem_history_owner_changed(&owner);
    ecu.control.mode = ENGINE_RUNNING;
    CHECK(!storage_save(0) && !ecu.storage_owner);
    CHECK(!oem_history_owner_save(&owner, &state, 0) && !ecu.storage_owner);
    ecu.control.mode = ENGINE_STOPPED;
    state.events.store.count = 21;
    CHECK(!oem_history_owner_save(&owner, &state, 0) && !ecu.storage_owner);
    state.events.store.count = 0;
    CHECK(storage_save(0));
    storage_poll(10000UL);
    CHECK(ecu.storage.result == 2 && !ecu.storage_owner);
    CHECK(storage_save(0));
    fake_eeprom_status = 2;
    storage_poll(0);
    fake_eeprom_status = 0;
    CHECK(ecu.storage.result == 3 && !ecu.storage_owner);
    CHECK(storage_save(0));
    fake_write_fail = 1;
    storage_poll(0);
    fake_write_fail = 0;
    CHECK(ecu.storage.result == 3 && !ecu.storage_owner);
}
static void test_inputs_and_configuration(void) {
    u16 error, i;
    u8 *c;
    setup();
    c = ecu.cal.bytes[0];
    c[0x600] = 1;
    CHECK(!cal_validate(c, &error));
    c[CAL_FLAGS] |= EQUIP_UPSTREAM_RELAY_HEATER;
    c[0x62C] = 0;
    /* User-supplied wiring: Spartan 0-5 V on AN6. No ready wire is invented;
       with qualification off (policy 0) a valid sample is used at once. */
    for (i = 10; i < 1010; i += 10) {
        adc_publish((u16)(0x6000U | i), i);
        sensors_update(i);
        CHECK(ecu.sensors.afr10 == (u16)(100U + (u32)i * 100UL / 1023UL));
        CHECK(ecu.sensors.wideband_ready == (ecu.sensors.oxygen.quality == QUALITY_VALID));
    }
    adc_publish(0x6000U, 1100);
    sensors_update(1100);
    CHECK(ecu.sensors.oxygen.quality == QUALITY_RANGE);
    adc_publish(0x6200U, 1110);
    sensors_update(1200);
    CHECK(ecu.sensors.oxygen.quality == QUALITY_STALE);
    put16(c + CAL_INJ_PHASE, 1680);
    CHECK(!cal_validate(c, &error));
    put16(c + CAL_INJ_PHASE, 1620);
    CHECK(cal_validate(c, &error));
    CHECK(cal_begin());
    put16(ecu.cal.bytes[1] + CAL_IAC_MAX, 200);
    ecu.iac.state = IAC_HOMING;
    CHECK(!cal_commit(1));
    CHECK(ecu.cal.active == 0);
    ecu.iac.state = IAC_READY;
    ecu.sensors.seeded[6] = 1;
    CHECK(cal_commit(1));
    CHECK(!ecu.sensors.seeded[6]);
    CHECK(ecu.iac.state == IAC_UNKNOWN);
}
static void test_clock_health(void) {
    u32 feeds;
    setup();
    fake_watchdog_services = 0;
    ecu_tick();
    ecu_poll();
    feeds = fake_watchdog_services;
    CHECK(feeds == 1);
    ecu_poll();
    ecu_poll();
    CHECK(fake_watchdog_services == feeds);
    ecu_tick_elapsed(4);
    CHECK(ecu.authority.inhibits & INH_DEADLINE);
    ecu_poll();
    CHECK(fake_watchdog_services == feeds + 1);
    CHECK(ecu.authority.inhibits & INH_DEADLINE);
    safety_inhibit(INH_OUTPUT, ecu.milliseconds);
    ecu_tick(); ecu_poll();
    CHECK(fake_watchdog_services == feeds + 2);
    CHECK(ecu.authority.inhibits & INH_OUTPUT);
    ecu_poll(); CHECK(fake_watchdog_services == feeds + 2);
    setup();
    ecu.foreground_stamp = 0;
    ecu.milliseconds = 50;
    ecu_tick();
    CHECK(ecu.authority.inhibits & INH_DEADLINE);
}
static void test_native_adc_frontend(void) {
    OemAdc native;
    u16 ch, word;
    setup();
    memset(&native, 0xA5, sizeof(native));
    CHECK(oem_adc_snapshot(&native, 0, 20) == 0);
    for (ch = 0; ch < 16; ch++) {
        word = (u16)((ch << 12) | 0x0C00U | (ch * 61U));
        adc_publish(word, 0xFFFFFFFCUL);
        CHECK(ecu.adc[ch].raw == ch * 61U);
        CHECK(ecu.adc[ch].result == word);
    }
    CHECK(oem_adc_snapshot(&native, 4, 8) == 65535U);
    for (ch = 0; ch < 16; ch++)
        CHECK(native.scan[15U - ch] == (u16)((ch << 12) | 0x0C00U | (ch * 61U)));
    CHECK(native.input[OEM_ADC_COOLANT] == 0xAE62U);
    CHECK(native.iat == 167U);
    CHECK(native.battery == 76U);
    CHECK(oem_adc_snapshot(&native, 4, 7) == 0);
    CHECK(native.input[OEM_ADC_COOLANT] == 0xAE62U); /* stale never becomes a passing value */
    adc_publish(0xB003U, 4);
    CHECK(oem_adc_snapshot(&native, 4, 0) == 0x0800U);
    CHECK(native.iat == 0);
    fake_irq = 0;
    CHECK(oem_adc_snapshot(&native, 4, 0) == 0x0800U);
    CHECK(!fake_irq);
    fake_irq = 1;
    /* Tunable sensor filtering/calibration cannot alter the native raw path. */
    sensors_update(4);
    CHECK(oem_adc_snapshot(&native, 4, 0) == 0x0800U);
    CHECK(native.iat == 0 && native.input[OEM_ADC_COOLANT] == 0xAE62U);
    /* An ISR publication after the caller's release timestamp is fresh. */
    ecu.milliseconds = 101;
    adc_publish(0x6200U, 101);
    adc_publish(0xB200U, 101);
    CHECK(oem_adc_snapshot(&native, 100, 0) == 0x0840U);
    CHECK(native.iat == 128U);
    sensors_update(100);
    CHECK(ecu.sensors.oxygen.quality == QUALITY_VALID);
    CHECK(ecu.sensors.iat.quality == QUALITY_VALID);
    /* A delayed pass must also recognize age measured at the actual read. */
    ecu.milliseconds = 201;
    CHECK(oem_adc_snapshot(&native, 101, 20) == 0);
    sensors_update(101);
    CHECK(ecu.sensors.oxygen.quality == QUALITY_STALE);
    CHECK(ecu.sensors.iat.quality == QUALITY_STALE);
    ecu.milliseconds = 1;
    adc_publish(0x6200U, 1);
    CHECK(oem_adc_snapshot(&native, 0xFFFFFFFFUL, 0) == 0x0040U);
    sensors_update(0xFFFFFFFFUL);
    CHECK(ecu.sensors.oxygen.quality == QUALITY_VALID);
}
static void test_standalone_dtc_controls(void) {
    DiagnosticMonitors monitors;
    OemDtcState events;
    u8 *cal;
    setup();
    cal = ecu.cal.bytes[0];
    memset(cal + CAL_DTC_ENABLE, 0, CAL_DTC_ENABLE_BYTES);
    memset(cal + CAL_DTC_SUBTYPE_ENABLE, 0, 107U);
    cal[CAL_DTC_ENABLE + 0x1BU / 8U] = (u8)(1U << (0x1BU & 7U));
    cal[CAL_DTC_SUBTYPE_ENABLE + 0x1BU] = 1U;
    memset(&events, 0, sizeof(events));
    diagnostic_monitors_init(&monitors);
    ecu.sensors.tps.quality = QUALITY_RANGE;
    ecu.adc[8].seen = 1;
    ecu.adc[8].raw = 900U;
    diagnostic_monitors_update(&monitors, &events, 10, cal);
    diagnostic_monitors_update(&monitors, &events, 20, cal);
    diagnostic_monitors_update(&monitors, &events, 30, cal);
    CHECK((events.live[0x1B] & 0x0F01U) == 0x0101U); /* P0123 */
    CHECK(diagnostic_monitor_subtype_enabled(cal, 0x1B, 1U));
    CHECK(!diagnostic_monitor_subtype_enabled(cal, 0x1B, 2U));
    cal[CAL_DTC_SUBTYPE_ENABLE + 0x1BU] = 8U;
    ecu.sensors.tps.quality = QUALITY_VALID;
    ecu.sensors.tps.value = 1000;
    diagnostic_monitors_update(&monitors, &events, 40, cal);
    ecu.sensors.tps.value = 0;
    diagnostic_monitors_update(&monitors, &events, 50, cal);
    ecu.sensors.tps.value = 1000;
    diagnostic_monitors_update(&monitors, &events, 60, cal);
    CHECK((events.live[0x1B] & 0x0F01U) == 0x0801U); /* P0121 */
    cal[CAL_DTC_SUBTYPE_ENABLE + 0x1BU] = 2U;
    ecu.sensors.tps.value = 0;
    diagnostic_monitors_update(&monitors, &events, 70, cal);
    CHECK(!(events.live[0x1B] & 1U));
    cal[CAL_DTC_ENABLE + 0x1BU / 8U] = 0;
    diagnostic_monitors_update(&monitors, &events, 80, cal);
    CHECK(!ecu.diagnostics.support[0x1B]);
    cal[CAL_DTC_ENABLE + 0x2DU / 8U] = (u8)(1U << (0x2DU & 7U));
    cal[0x600] = 1; /* wideband */
    diagnostic_monitors_update(&monitors, &events, 90, cal);
    CHECK(!ecu.diagnostics.support[0x2D]);

    setup();
    cal = ecu.cal.bytes[0];
    memset(cal + CAL_DTC_ENABLE, 0, CAL_DTC_ENABLE_BYTES);
    cal[CAL_DTC_ENABLE + 0x1AU / 8U] |= (u8)(1U << (0x1AU & 7U));
    cal[CAL_DTC_ENABLE + 0x4BU / 8U] |= (u8)(1U << (0x4BU & 7U));
    memset(&events, 0, sizeof(events));
    diagnostic_monitors_init(&monitors);
    ecu.control.mode = ENGINE_RUNNING;
    ecu.rotation.epoch = 7;
    ecu.rotation.state = ROT_VALID;
    diagnostic_phase_arm(1000, 7, 0);
    diagnostic_phase_capture(1100); /* valid delay, establishes previous */
    diagnostic_monitors_update(&monitors, &events, 10, cal);
    CHECK(!monitors.phase_valid);
    diagnostic_phase_arm(2000, 7, 0);
    diagnostic_phase_capture(2120); /* +20 ticks, over the OEM 18-tick delta */
    diagnostic_monitors_update(&monitors, &events, 20, cal);
    CHECK(monitors.phase_valid && monitors.phase_cylinder_one);
    diagnostic_phase_arm(3000, 7, 0);
    diagnostic_phase_arm(3100, 7, 0); /* unconsumed arm is a missing capture */
    diagnostic_monitors_update(&monitors, &events, 30, cal);
    CHECK(monitors.fail[0x1A] == 1U && !monitors.phase_valid);
}
unsigned test_audit_regressions(void);
static void test_capture_detail_packet(void) {
    static const u8 request[] = {0xAA, 2, 0x2F, 2, 0x33};
    u8 i;
    setup();
    timing_health.pass_max_ms = 21;
    timing_health.capture_overruns = 4;
    timing_health.capture_resyncs = 3;
    timing_health.capture_counter = 0;
    timing_health.capture_expected = 65535;
    timing_health.capture_last = 0x1234;
    timing_health.capture_latest = 0x5678;
    timing_health.capture_irq = 0x00FE;
    timing_health.capture_pec = 0x0200;
    timing_health.capture_dest = 0xF65A;
    timing_health.capture_head = 2;
    timing_health.capture_tail = 1;
    timing_health.capture_next = 0;
    timing_health.capture_count = 30;
    for (i = 0; i < 30U; i++) timing_health.capture_words[i] = (u16)(0x8000U + i);
    timing_health.capture_reason = 2;
    feed(request, sizeof(request), 0);
    CHECK(ecu.protocol.tx[1] == 96 && ecu.protocol.tx[2] == 2);
    CHECK(get16(ecu.protocol.tx + 4) == 21);
    CHECK(get16(ecu.protocol.tx + 12) == 4);
    CHECK(get16(ecu.protocol.tx + 16) == 2);
    CHECK(get16(ecu.protocol.tx + 18) == 0);
    CHECK(get16(ecu.protocol.tx + 20) == 65535);
    CHECK(get16(ecu.protocol.tx + 22) == 0x1234);
    CHECK(get16(ecu.protocol.tx + 24) == 0x5678);
    CHECK(get16(ecu.protocol.tx + 26) == 0x00FE);
    CHECK(get16(ecu.protocol.tx + 28) == 0x0200);
    CHECK(get16(ecu.protocol.tx + 30) == 0xF65A);
    CHECK(ecu.protocol.tx[32] == 2 && ecu.protocol.tx[33] == 1);
    CHECK(ecu.protocol.tx[34] == 0 && ecu.protocol.tx[35] == 30);
    CHECK(get16(ecu.protocol.tx + 36) == 3);
    CHECK(get16(ecu.protocol.tx + 38) == 0x8000 && get16(ecu.protocol.tx + 96) == 0x801D);
    CHECK(timing_health.pass_max_ms == 21 && timing_health.capture_reason == 2);
    setup();
    timing_health.capture_resyncs = 5;
    timing_health.capture_counter = 123; /* unpublished snapshot must stay hidden */
    timing_health.capture_words[0] = 77;
    feed(request, sizeof(request), 0);
    CHECK(get16(ecu.protocol.tx + 16) == 0 && get16(ecu.protocol.tx + 18) == 0);
    CHECK(get16(ecu.protocol.tx + 36) == 5 && get16(ecu.protocol.tx + 38) == 0);
}
#include "test_idle_dfco.inc"
#include "test_knock.inc"
int main(void) {
    test_knock();
    test_idle_dfco();
    test_math_cal();
    test_rotation();
    test_rotation_block();
    test_table_math();
    test_authority();
    test_parser();
    test_iac();
    test_iac_direction_and_status();
    test_fan_preload();
    test_controls();
    test_fuel_percentages();
    test_inputs_and_configuration();
    test_clock_health();
    test_capture_detail_packet();
    test_native_adc_frontend();
    test_standalone_dtc_controls();
    test_storage();
    test_history_storage();
    test_history_schema();
    test_persistence_owner();
    tests += test_lifecycle();
    tests += test_faults();
    tests += test_audit_regressions();
    printf("PASS %u assertions\n", tests);
    return 0;
}
