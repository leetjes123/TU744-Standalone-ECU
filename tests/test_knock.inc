#include "knock.h"
static KnockConfig knock_fixture(void) {
    KnockConfig cfg;
    u8 i;
    setup();
    ecu.rotation.state = ROT_VALID; ecu.control.mode = ENGINE_RUNNING;
    ecu.authority.inhibits = 0;
    memset(&cfg, 0, sizeof(cfg));
    cfg.generation = ecu.cal.generation; cfg.epoch = ecu.rotation.epoch;
    cfg.mode = KNOCK_CONTROL; cfg.eligible = 1;
    cfg.threshold = 40; cfg.divisor = 16; cfg.attack = 4; cfg.maximum = 16;
    cfg.hold = 4; cfg.debounce = 3; cfg.null_tolerance = 25; cfg.test_shift = 179;
    cfg.drift_limit = 21; cfg.latch_ms = 500; cfg.stale_ms = 150;
    for (i = 0; i < 7; i++) cfg.gain_code[i] = (u8)(i < 4 ? i : i + 1);
    knock_config = cfg; knock.mode = cfg.mode;
    knock.good_count = 3; knock.qualified = 1;
    knock.reference = 32; knock.gain = 4; knock.gain_code = 5;
    return cfg;
}
static void test_knock(void) {
    KnockConfig cfg = knock_fixture();
    KnockState state;
    u8 packet[64], i;
    u16 raw, error;
    u32 count;
    /* All ADC codes, equality and overload boundaries; each isolated case
       resets adaptation so expected thresholds are independently calculable. */
    for (raw = 0; raw < 1024; raw++) {
        state = knock; state.reference = 32; state.offset = state.null_start = 37;
        knock_detect(&state, &cfg, (u8)(raw >> 2));
        CHECK(state.amplitude == ((raw >> 2) > 37 ? (raw >> 2) - 37 : 0));
        CHECK(state.decision == (raw >= 468));
        CHECK(knock_mv(raw) == (raw * 5000UL + 512UL) / 1024UL);
    }
    cfg.threshold = 80; state = knock; state.reference = 64;
    knock_detect(&state, &cfg, 226); CHECK(!state.decision);
    state.reference = 64; knock_detect(&state, &cfg, 227); CHECK(state.decision);
    cfg.threshold = 40;
    for (i = 0; i < 5; i++) {
        knock.reference = 32;
        knock_sample(&cfg, 468, KNOCK_NORMAL, 1250, 100 + i);
        CHECK(knock.retard == (i < 4 ? (i + 1) * 4 : 16));
        CHECK(knock.hold == 4 && knock.count == (u32)i + 1);
    }
    CHECK(knock.ceiling_events == 1);
    CHECK(knock_advance(20) == 4 && knock.scheduled == 16);
    CHECK(knock_advance(140) == 124 && knock.scheduled == 3);
    CHECK(knock_advance(-125) == -128 && knock.scheduled == 3);
    count = knock.count;
    knock_null_start(148);
    knock_sample(&cfg, 148, KNOCK_NULL, 1250, 110);
    knock_sample(&cfg, 864, KNOCK_TEST, 1250, 120);
    CHECK(knock.count == count && knock.retard == 16 && knock.hold == 4);
    CHECK(knock.raw == 468 && knock.stamp == 104 && knock.type == KNOCK_NORMAL);
    for (i = 0; i < 4; i++) {
        knock_sample(&cfg, 148, KNOCK_NORMAL, 1250, 130 + i);
        CHECK(knock.retard == (i < 3 ? 16 : 15));
    }
    CHECK(knock.hold == 4);
    knock_monitor(packet, 603); CHECK(packet[2] & 1);
    knock_monitor(packet, 604); CHECK(!(packet[2] & 1));
    CHECK(!(packet[2] & 2) && get16(packet) == knock_mv(148));
    knock_invalidate(KNOCK_FAULT_ADC);
    CHECK(knock.retard == 15 && !knock.qualified && !knock.valid);
    knock_sample(&cfg, 148, KNOCK_NORMAL, 1250, 700);
    CHECK(knock.retard == 15 && !knock.valid && !knock.decision);
    cfg = knock_fixture(); cfg.mode = KNOCK_MONITOR; knock.mode = cfg.mode;
    knock_sample(&cfg, 468, KNOCK_NORMAL, 1250, 0xFFFFFF00UL);
    CHECK(knock.count == 1 && !knock.retard && knock_advance(20) == 20);
    knock_monitor(packet, 100); CHECK(packet[2] & 1);
    knock_monitor(packet, 244); CHECK(!(packet[2] & 1));
    cfg = knock_fixture(); cfg.manual_gain = 1;
    state = knock; state.reference = 16; knock_detect(&state, &cfg, 37);
    CHECK(state.gain == 4);
    cfg.manual_gain = 0; state.reference = 16;
    knock_detect(&state, &cfg, 37); CHECK(state.gain == 5 && state.reference == 30);
    state.reference = 0; knock_detect(&state, &cfg, 117);
    CHECK(state.fault == KNOCK_FAULT_REFERENCE && !state.decision);
    cfg = knock_fixture(); cfg.epoch++;
    knock_sample(&cfg, 468, KNOCK_NORMAL, 1250, 10);
    CHECK(!knock.valid && !knock.count && knock.fault == KNOCK_FAULT_STALE);
    cfg = knock_fixture(); cfg.generation++;
    knock_sample(&cfg, 468, KNOCK_NORMAL, 1250, 10);
    CHECK(!knock.valid && !knock.count);
    cfg = knock_fixture();
    knock_details(packet, 10); CHECK(packet[0] == 1 && get16(packet + 1) == 65535);
    /* Real framed protocol replies retain the exact legacy envelope and
       append the same knock bytes to the compact frame. */
    knock_sample(&cfg, 468, KNOCK_NORMAL, 1250, 10);
    { static const u8 details[] = {0xAA,1,0x34,0x35};
      static const u8 legacy[] = {0xAA,1,0x10,0x11};
      static const u8 compact[] = {0xAA,1,0x13,0x14};
      static const u8 job[] = {0xAA,2,0x35,3,0x3A};
      feed(details,sizeof(details),10);
      CHECK(ecu.protocol.tx[1] == 64 && ecu.protocol.tx[2] == 1);
      CHECK(get16(ecu.protocol.tx+7) == 468 && ecu.protocol.tx[37] == knock.scheduled);
      while (ecu.protocol.tx_length) protocol_poll(10);
      ecu.milliseconds = 10;
      feed(legacy,sizeof(legacy),10);
      CHECK(ecu.protocol.tx[1] == 98 && get16(ecu.protocol.tx+96) == 2285);
      CHECK(ecu.protocol.tx[98] == 199 && ecu.protocol.tx[99] == 4);
      while (ecu.protocol.tx_length) protocol_poll(10);
      feed(compact,sizeof(compact),10);
      CHECK(ecu.protocol.tx[1] == 44 && ecu.protocol.tx[2] == 3);
      CHECK(get16(ecu.protocol.tx+42) == 2285 && ecu.protocol.tx[45] == 4);
      while (ecu.protocol.tx_length) protocol_poll(10);
      feed(job,sizeof(job),10); CHECK(ecu.protocol.tx[1] == 86 && ecu.protocol.tx[66] == 1);
    }
    ecu.cal.bytes[0][CAL_KNOCK_MANUAL_GAIN] = 2;
    CHECK(!cal_validate(cal_active(), &error));
    ecu.cal.bytes[0][CAL_KNOCK_MANUAL_GAIN] = 1;
    CHECK(cal_validate(cal_active(), &error));
    ecu.cal.bytes[0][CAL_KNOCK_ATTACK_STEP] = 17;
    CHECK(!cal_validate(cal_active(), &error));
    /* Generation zero is valid after u16 wrap and after loading a tune. */
    setup(); ecu.cal.generation = 0;
    ecu.cal.bytes[0][CAL_KNOCK_MODE] = KNOCK_CONTROL;
    ecu.cal.bytes[0][CAL_KNOCK_GAIN] = 2;
    ecu.cal.bytes[0][CAL_KNOCK_MANUAL_GAIN] = 1;
    knock_update(0, &ecu.rotation, cal_active());
    CHECK(knock.mode == KNOCK_CONTROL && knock.gain == 2 && knock_config.manual_gain);
}
