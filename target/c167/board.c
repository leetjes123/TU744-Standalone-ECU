#include "board.h"
#include "lifecycle.h"
#include "diagnostic_monitors.h"
#include <intrins.h>
u8 hal_run_permission(void) {
    /* ROM726D6 ->2B930 ->FD08.13; connector identity remains unresolved. */
    return (u8)((P4 >> 4) & 1U);
}
u8 hal_power_release(void) {
    /* ROM68C40 raises P3.12, but its load/board variant is unverified.
       Do not drive a guessed latch. Expose POWER_HELD to the tool instead. */
    return 0;
}
void hal_system_reset(void) {
    /* TU5JP68B1A uses SRST when raw run permission returns in alternate mode.
       The owner first drains active outputs and any in-flight storage write. */
    IEN = 0;
    hal_cancel_all();
    __asm {SRST}
}
void hal_phase_arm(void) SHARED {
    CC8IR = 0;
    CC8IE = 1;
}
void hal_phase_disarm(void) SHARED {
    CC8IE = 0;
    CC8IR = 0;
    phase_observation.armed = 0;
}
u32 hal_capture_clock(void) {
    u16 lock = IEN, high, low;
    u32 ticks;
    IEN = 0;
    high = capture_high;
    low = T1;
    if (T1IR) {
        high++;
        low = T1;
    }
    ticks = ((u32)high << 16) | low;
    IEN = lock;
    return ticks;
}
volatile u16 capture_high;
static volatile u8 gauge_duty, boost_duty, gauge_phase, adc_phase;
#define ADC_SCAN_MS 5U
static volatile u8 echo_pending, echo_byte;
static volatile u32 echo_deadline;
static u32 tick_capture;
static u16 tick_fraction;
static u8 tick_seeded;
/* Critical sections raise the CPU level to 14 rather than clearing IEN.
   Crank capture (PEC channel 6, its block handler) and the T1 overflow that
   extends its clock run at level 15, so no critical section, however slow
   the external bus, can hold a capture transfer past the next tooth (a lost
   capture latches DEADLINE). Code sharing state with level 15 uses the
   hal_hard_lock mask instead. The returned word holds ILVL and IEN. */
u16 hal_lock(void) SHARED {
    u16 old = PSW & 0xF800U;
    if (old < 0xE000U)
        PSW = (PSW & 0x0FFFU) | 0xE000U;
    return old;
}
void hal_unlock(u16 old) SHARED {
    PSW = (PSW & 0x07FFU) | old;
}
u16 hal_hard_lock(void) {
    u16 old = IEN;
    IEN = 0;
    return old;
}
void hal_hard_unlock(u16 old) {
    IEN = old;
}
void hal_aux(u8 pump, u8 fan, u8 boost, u8 gauge, u8 heaters, u8 mil) {
    u16 lock = hal_lock();
    if (!BOARD_RELEASED || !safety_aux_permitted()) {
        pump = 0;
        boost = 0;
        heaters = 0;
    }
    PIN_PUMP = !pump;
    PIN_FAN_A = !fan;
    PIN_FAN_B = !fan;
    PIN_HEATER_UP = !(heaters & 1);
    PIN_HEATER_DOWN = !(heaters & 2);
    PIN_MIL = !mil;
    gauge_duty = gauge;
    boost_duty = ecu.authority.inhibits ? 0 : boost;
    if (!boost_duty || boost_duty >= 100) {
        CCM4 &= 0x0FFFU;
        PIN_BOOST = !boost_duty;
    } else if ((CCM4 & 0xF000U) != 0xF000U) {
        CC19 = (u16)(60926UL + (u32)boost_duty * 4610UL / 100UL);
        CC19IR = 0;
        T8 = 60926;
        CCM4 = (CCM4 & 0x0FFFU) | 0xF000U;
        PIN_BOOST = 0;
    }
    hal_unlock(lock);
}
void hal_watchdog_service(void) {
    WDTCON = 0x8001;
    __asm {SRVWDT}
}
void hal_uart_send(u8 byte) {
    u16 lock = hal_lock();
    echo_byte = byte;
    echo_pending = 1;
    echo_deadline = ecu.milliseconds + 3;
    S0TIR = 0;
    S0TBUF = byte;
    hal_unlock(lock);
}
u8 hal_uart_ready(void) {
    u16 lock = hal_lock();
    u8 ready;
    if (echo_pending && (s32)(ecu.milliseconds - echo_deadline) >= 0)
        echo_pending = 0;
    ready = (u8)(S0TIR && !echo_pending);
    hal_unlock(lock);
    return ready;
}
/* CC195 band-pass code BF3..BF0 for a nominal kHz value: OEM sub_493DC,
   jump table file 0x122A0. 11, 13, 15 and out-of-range values give 0. */
static u8 knock_bf_code(u8 khz) {
    static const u8 codes[12] = {0x8, 0xA, 0xB, 0xC, 0x6, 0x0, 0x0, 0x2, 0x0, 0x3, 0x0, 0x4};
    return (khz >= 5U && khz <= 16U) ? codes[khz - 5U] : 0U;
}
/* OEM sub_493BC (0x493D0..0x493D8) then sub_493DC's P8.5 write. */
void board_knock_ic_release(u8 filter_khz) {
    PIN_KNOCK_G0 = 1;   /* gain code 101 = x32, index 4 */
    PIN_KNOCK_G1 = 0;
    PIN_KNOCK_G2 = 1;
    PIN_KNOCK_KTI = 0;  /* test pulse off */
    PIN_KNOCK_KSA3 = 0; /* sensor input connected */
    PIN_KNOCK_BF2 = (bit)((knock_bf_code(filter_khz) >> 2) & 1U);
}
/* The complete OEM knock-IC boot timeline at the CC195 pins, from reset:
   port init at the OEM instant (sub_32886), the OEM hold, then the release.
   T1 (fCPU/16) has counted since reset (START167.A66) and overflows once,
   at 52.43 ms, before the OEM port-init instant; board_init cleared T1IR
   earlier and interrupts are still disabled. Constants and their Keil
   measurement are in board.h. */
void board_knock_ic_boot(u8 filter_khz) {
    u16 mf_low;
    u8 i;
    while (!T1IR)
        ;
    while (T1 < KNOCK_IC_PORT_T1)
        ;
    /* sub_32886: P3 latch 0xB5FE, DP3 0x35FE: G2..G0 = 111, KTI = 1,
       KSA3 = 1 driven (the IC's pull-ups already hold these levels). */
    P3 |= 0x006EU;
    DP3 |= 0x006EU;
    /* The OEM writes P4, P6 and P7 in between: same gap to the P8 writes. */
    for (i = 0; i < KNOCK_IC_P3_P8_NOPS; i++)
        _nop_();
    /* P8 latch 0xAE, DP8 0xAF: MF = 0 (integrator reset), BF2 = 1. */
    P8 = (P8 & ~0x0001U) | 0x0020U;
    DP8 |= 0x0021U;
    mf_low = T1;
    while ((u16)(T1 - mf_low) < KNOCK_IC_HOLD_T1)
        ;
    board_knock_ic_release(filter_khz);
}
void board_init(void) {
    IEN = 0;
    /* Knock IC lines stay inputs here (the IC's pull-ups hold them, as under
       the OEM before its port init); board_knock_ic_boot drives them. */
    P2 |= 0x2003;
    DP2 |= 0x2003;
    DP2 &= 0x3FFF;
    DP2 &= ~0x0300U; /* OEM CC8 phase and CC9 dwell-feedback inputs. */
    P7 |= 0x76;
    DP7 |= 0x76;
    P8 |= 0x8E;
    DP8 |= 0x8E;
    P3 |= 0xA610;
    DP3 |= 0xA610;
    DP3 &= ~0x0900U;
    DP3 &= ~1U; /* T0IN: the OEM crank-edge counter input. */
    P6 |= 0x14;
    DP6 |= 0x14;
    P4 |= 0x80;
    DP4 |= 0x80;
    DP4 &= ~0x10U; /* OEM run-permission input P4.4. */
    CCM0 = CCM1 = CCM2 = CCM3 = CCM4 = CCM5 = CCM6 = CCM7 = 0;
    CCM2 = 0x00A9U; /* CC8 rising, CC9 falling, both against T1. */
    CC8IC = IRQ(9, 0);
    CC8IE = 0;
    CC9IC = IRQ(9, 1); CC9IE = 0;
    board_outputs_init();
    /* Capture the OEM-selected falling crank edge with free-running T1. */
    T1REL = 0; /* T1 already runs from reset (START167.A66): keep its count. */
    T0 = 0; T0REL = 0; T0IC = 0;
    T01CON = 0x414A; /* OEM falling-edge T0 counter + free-running T1. */
    T1IC = IRQ(15, 3); PECC7 = 0; /* extends the crank clock; same level as capture */
    T7REL = 0;
    T7 = 0;
    T78CON = 0x0041;
    T7IC = 0;
    T8REL = 60926;
    T8 = 60926;
    T78CON |= 0x4400;
    T8IC = 0;
    CC19IC = IRQ(6, 0);
    T5CON = 0;
    T5 = 0;
    T5IC = IRQ(6, 1);
    CC27IC = IRQ(6, 2);
    PIN_TACH = 0;
    CC15IC = IRQ(15, 2); /* PEC6: above every hal_lock section */
    /* Deferred crank decode runs below the 1 ms tick (level 10): a 30-tooth
       block took up to ~2.5 ms before the block decoder and must never delay
       the tick past its 3 ms DEADLINE bound. Crank capture (PEC, 15) and
       output compares (12/13/14) remain above both. */
    XP1IC = IRQ(9, 2);
    board_capture_init();
    CC14IC = IRQ(5, 0);
    CCM3 = 0xA900;
    CC30IC = IRQ(12, 3);
    CC29IC = IRQ(12, 2);
    CC28IC = IRQ(12, 1);
    CC23IC = IRQ(12, 0);
    CC20IC = IRQ(14, 0); PECC0 = 0;
    CC21IC = IRQ(14, 1); PECC1 = 0;
    CC0IC = IRQ(13, 3);
    CC1IC = IRQ(13, 2); /* coil B fire compare: CC1IO = P2.1 */
    CC6IC = IRQ(13, 1);
    CC4IC = IRQ(13, 0);
    CC24IC = CC25IC = 0;
    T6 = 1249;
    CAPREL = 1249;
    T6CON = 0x80C2;
    T6IC = IRQ(10, 0);
    /* Single auto-scan of AN15..AN0 (ADM=10), restarted from the tick every
       ADC_SCAN_MS. Continuous scanning raised one interrupt per ~42 us
       conversion (17-24k/s, ~1/3 of the CPU at the programmed bus timing)
       while control consumes one filtered sample per channel per 10 ms. */
    ADCON = 0xF02F;
    ADCIC = IRQ(8, 0);
    ADCON |= 0x0080;
    /* SSCBR must not be written while SSCEN=1 (C167CR UM p.280); the OEM
       init also clears SSCEN, loads SSCBR, then enables. */
    SSCCON = 0x0000;
    SSCBR = 0x0080;
    SSCCON = 0xC057;
    /* Hold edges/RX may preempt the noncritical part of the 1 ms T6 tick.
       Every shared owner mutation still holds hal_lock (ILVL14), including
       board_ssc_tick, so the owner stays serialized. Engine stages and crank
       remain higher priority. Do not spend the 200 us hold budget on T6. */
    SSCRIC = IRQ(11, 1);
    SSCRIE = 0;
    CC22IC = IRQ(11, 2);
    CC22IE = 0;
    SSCTIC = 0;
    SSCEIC = 0;
    S0BG = 0x1F;
    S0CON = 0x8011;
    /* Above the crank worker and the tick: ASC0 holds one received byte
       (0.5 ms at this baud rate) and a 30-tooth block takes ~1 ms of XP1 at
       10,000 rpm. The wire limits this to one short interrupt per 0.5 ms. */
    S0RIC = IRQ(11, 0);
    S0TIC = 0;
    S0TIR = 1;
    WDTCON = 0x0001;
    __asm {SRVWDT}
}
void phase_isr(void) interrupt 0x18 {
    /* T1 overflow has higher priority. Snapshot all three words coherently. */
    u16 lock = hal_hard_lock();
    u16 captured = CC8, current = T1, high = capture_high;
    u32 stamp;
    if (T1IR) {
        high++;
        current = T1;
    }
    if (captured > current)
        high--;
    hal_hard_unlock(lock);
    stamp = ((u32)high << 16) | captured;
    lock = hal_lock();
    diagnostic_phase_capture(stamp);
    CC8IE = 0;
    hal_unlock(lock);
}
void overflow_isr(void) interrupt 0x21 {
    capture_high++;
    XP1IR = 1; /* The deferred worker owns rotation mutations and timeout. */
}
void tick_isr(void) interrupt 0x26 {
    u16 lock = hal_hard_lock(), high = capture_high, low = T1, elapsed;
    u32 captured, delta;
    if (T1IR) {
        high++;
        low = T1;
    }
    captured = ((u32)high << 16) | low;
    /* Mask only the coherent clock snapshot. Holding interrupts off for the
       whole tick (55-165 us) delayed staged charge compares past their tooth
       at ~9,500 rpm (105 us per tooth); refine then skipped the spark. The
       tick state below is private; callees lock their own shared updates
       (milliseconds, inhibits, SSC). */
    hal_hard_unlock(lock);
    if (!tick_seeded) {
        tick_seeded = 1;
        delta = 1250UL;
    } else
        delta = captured - tick_capture;
    tick_capture = captured;
    delta += tick_fraction;
    /* Normally one tick: subtract instead of a 32-bit library divide. */
    if (delta < 5000UL) {
        elapsed = 0;
        while (delta >= 1250UL) {
            delta -= 1250UL;
            elapsed++;
        }
        tick_fraction = (u16)delta;
    } else {
        elapsed = (u16)(delta / 1250UL);
        tick_fraction = (u16)(delta % 1250UL);
    }
    ecu_tick_elapsed(elapsed);
    board_ssc_tick();
    if (++adc_phase >= ADC_SCAN_MS) {
        adc_phase = 0;
        if (!ADBSY)
            ADST = 1;
    }
    gauge_phase++;
    if (gauge_phase >= 10) {
        gauge_phase = 0;
        T5R = 0;
        T5IR = 0;
        if (!gauge_duty)
            PIN_GAUGE = 1;
        else {
            PIN_GAUGE = 0;
            if (gauge_duty < 100) {
                T5 = (u16)(65536UL - (u32)gauge_duty * 500UL);
                T5R = 1;
            }
        }
    }
    if (ecu.authority.inhibits) {
        CCM4 &= 0x0FFFU;
        PIN_BOOST = 1;
        boost_duty = 0;
    }
    if (!safety_aux_permitted()) {
        PIN_PUMP = 1;
        PIN_HEATER_UP = 1;
        PIN_HEATER_DOWN = 1;
    }
}
void adc_isr(void) interrupt 0x28 {
    u16 result = ADDAT;
    u16 lock = hal_lock();
    u32 stamp = ecu.milliseconds;
    /* T6 has higher priority: the two clock words must be read together. */
    hal_unlock(lock);
    adc_publish(result, stamp);
}
void vss_isr(void) interrupt 0x1E {
    u16 lock = hal_lock();
    ecu.vss_count++;
    ecu.vss_edge_stamp = ecu.milliseconds;
    hal_unlock(lock);
}
void receive_isr(void) interrupt 0x2B {
    u8 byte = (u8)S0RBUF;
    if (echo_pending && byte == echo_byte) {
        echo_pending = 0;
        protocol_tx_continue();
        return;
    }
    echo_pending = 0;
    protocol_receive(byte);
}
void gauge_isr(void) interrupt 0x25 {
    PIN_GAUGE = 1;
    T5R = 0;
}
void boost_isr(void) interrupt 0x33 {
    CC19 = (u16)(60926UL + (u32)boost_duty * 4610UL / 100UL);
}
