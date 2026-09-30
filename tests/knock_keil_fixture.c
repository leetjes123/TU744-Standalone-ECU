/* Simulator-only entry point. Linked against the unchanged production objects
   except main.obj. Analog stimuli are supplied at fixture_input by the runner. */
#include "board.h"
#include "knock.h"
#include "lifecycle.h"
#include <intrins.h>
volatile u16 fixture_index, fixture_raw, fixture_ok, fixture_saved, fixture_ticks;
volatile u8 fixture_monitor[4];
volatile u16 fixture_job_state, fixture_job_windows, fixture_job_fault, fixture_service;
void fixture_input(void) { _nop_(); }
void fixture_result(void) { _nop_(); }
void fixture_done(void) { for (;;) { _nop_(); } }
void main(void) {
    u16 start;
    u8 i;
    board_init(); ecu_init(1); board_knock_init();
    DP3 |= 0x006E; DP8 |= 0x0021;
    /* Keep the converter scan running, without unrelated foreground work. */
    T6IC = ADCIC = CC15IC = XP1IC = SSCRIC = CC22IC = S0RIC = 0;
    cal_example(ecu.cal.bytes[0]); ecu.cal.valid = 1; ecu.cal.generation = 1;
    ecu.rotation.state = ROT_VALID; ecu.control.mode = ENGINE_RUNNING;
    ecu.authority.inhibits = 0;
    __asm { EINIT }
    IEN = 1;
    knock_config.generation = 1; knock_config.epoch = ecu.rotation.epoch;
    knock_config.mode = KNOCK_CONTROL; knock_config.eligible = 1;
    knock_config.divisor = 16; knock_config.attack = 4; knock_config.maximum = 16;
    knock_config.hold = 4; knock_config.debounce = 3;
    knock_config.latch_ms = 500; knock_config.stale_ms = 150;
    knock_config.null_tolerance = 25; knock_config.test_shift = 179;
    for (i = 0; i < 7; i++) knock_config.gain_code[i] = (u8)(i < 4 ? i : i + 1);
    knock.mode = KNOCK_CONTROL;
    for (fixture_index = 0; fixture_index < 10; fixture_index++) {
        knock.reference = fixture_index == 5 || fixture_index == 6 ? 64 : 32;
        knock_config.threshold = fixture_index == 5 || fixture_index == 6 ? 80 : 40;
        knock.good_count = 3; knock.qualified = 1; knock.gain = 4;
        knock_config.mode = fixture_index == 7 ? KNOCK_MONITOR : KNOCK_CONTROL;
        fixture_raw = 65535;
        ADDAT2 = 0xB155;
        fixture_input();
        start = T7;
        fixture_ok = hal_knock_sample((u16 *)&fixture_raw);
        fixture_ticks = (u16)(T7 - start);
        fixture_saved = ADDAT2;
        if (fixture_ok) knock_sample(&knock_config, fixture_raw,
            fixture_index == 8 ? KNOCK_NULL : fixture_index == 9 ? KNOCK_TEST : KNOCK_NORMAL,
            1250, fixture_index * 10UL);
        knock_monitor((u8 *)fixture_monitor, fixture_index * 10UL);
        fixture_result();
        hal_watchdog_service();
    }
    ecu.control.mode = ENGINE_STOPPED; ecu.rotation.state = ROT_UNSYNCED;
    ecu.rotation.rpm = 0; ecu.rotation.seen = 0; ecu.key_input = 1;
    knock.retard = 0;
    fixture_index = 10;
    fixture_ok = knock_job(2, 0);
    while (knock.bench) {
        ecu.milliseconds = hal_capture_clock() / 1250UL;
        hal_knock_bench(knock.bench, ecu.milliseconds);
        hal_watchdog_service();
    }
    fixture_job_state = knock_job_result.state;
    fixture_job_windows = knock_job_result.completed;
    fixture_job_fault = knock_job_result.fault;
    fixture_service = ecu.service;
    fixture_done();
}
