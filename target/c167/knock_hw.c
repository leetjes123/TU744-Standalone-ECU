#include "board.h"
#include "knock.h"
#include "lifecycle.h"
/* CC2/T0 reference and CC16/T7 window owner. No calibration-bank pointers. */
static KnockConfig window, pending_config;
static u16 pending_raw, pending_ticks, boundary_epoch;
static u32 pending_at, bench_at;
static u8 phase, pending, sample_type, pending_type, adc_busy, job_phase;
static u8 pending_gain, window_gain;
static u16 window_length, health_count;
static void gain(u8 code) {
    PIN_KNOCK_G0 = (bit)(code & 1U);
    PIN_KNOCK_G1 = (bit)((code >> 1) & 1U);
    PIN_KNOCK_G2 = (bit)((code >> 2) & 1U);
}
void hal_knock_cancel(void) {
    CC2IE = CC16IE = 0;
    CC2IR = CC16IR = 0;
    phase = pending = 0;
    PIN_KNOCK_MF = 0;
    PIN_KNOCK_KTI = 0;
    PIN_KNOCK_KSA3 = 0;
    gain(knock.gain_code);
}
void hal_knock_configure(u8 filter) { board_knock_ic_release(filter); }
/* The capture owner calls this on the first edge, before rotation decoding.
   No lock here: capture runs at level 15 and must never lower its priority. */
void board_knock_crank_seen(void) {
    if (!knock.bench) return;
    knock.bench = knock.valid = knock.decision = knock.recent = knock.qualified = 0;
    knock_job_result.state = 4;
    hal_knock_cancel();
}
void board_knock_init(void) {
    CCM0 = (CCM0 & 0xF0FFU) | 0x0400U; /* CC2 compare mode 0 on T0 */
    CCM4 = (CCM4 & 0xFFF0U) | 0x0004U; /* CC16 compare mode 0 on T7 */
    CC2IC = IRQ(6, 3); CC2IE = 0;
    CC16IC = IRQ(5, 3); CC16IE = 0;
    phase = pending = adc_busy = job_phase = 0;
    health_count = 0;
}
/* At maximum ADCTC/ADSTC, a scan conversion is approximately 42 us.
   Budget includes an in-flight scan plus injection and interrupt preemption.
   The 2 ms deadline is a scheduling bound; bench/Keil timing must verify it. */
u8 hal_knock_sample(u16 *raw) {
    u16 lock, saved, start, spins = 0;
    u8 ok;
    lock = hal_lock();
    if (adc_busy) { hal_unlock(lock); return 0; }
    adc_busy = 1; saved = ADDAT2;
    ADDAT2 = 0xF000U; ADCRQ = 1;
    start = T7;
    hal_unlock(lock);
    while (ADCRQ && (u16)(T7 - start) < 2500U && ++spins < 10000U) { }
    lock = hal_lock();
    ok = (u8)!ADCRQ;
    if (ok) *raw = ADDAT2 & 0x03FFU;
    else ADCRQ = 0;
    ADDAT2 = saved; adc_busy = 0;
    hal_unlock(lock);
    return ok;
}
static void fault(u8 reason) {
    if (reason == KNOCK_FAULT_ADC) {
        if (knock.adc_timeouts != 65535U) knock.adc_timeouts++;
    } else if (knock.missed_windows != 65535U) knock.missed_windows++;
    knock_invalidate(reason);
}
static u8 valid_window(void) {
    return (u8)(window.generation == ecu.cal.generation &&
        window.epoch == ecu.rotation.epoch &&
        (knock.bench ? (!ecu.rotation.rpm && ecu.control.mode == ENGINE_STOPPED) :
         (ecu.rotation.state == ROT_VALID && !ecu.service && !ecu.authority.inhibits && window.mode)));
}
void board_knock_reference(u16 counter, u32 stamp) {
    u16 lock, target = (u16)(counter + 18U), distance;
    if (knock.bench) { knock.bench = 0; knock_invalidate(0); return; }
    if (!knock_config.mode || ecu.rotation.state != ROT_VALID || ecu.authority.inhibits ||
        ecu.control.mode != ENGINE_RUNNING || ecu.rotation.rpm < 480U) {
        hal_knock_cancel(); return;
    }
    lock = hal_lock();
    distance = (u16)(target - T0);
    if (!distance || distance > 18U ||
        hal_capture_clock() - stamp >= ecu.rotation.normal * 18UL) {
        fault(KNOCK_FAULT_WINDOW); hal_unlock(lock); return;
    }
    boundary_epoch = ecu.rotation.epoch;
    CC2IE = 0; CC2 = target; CC2IR = 0; CC2IE = 1;
    hal_unlock(lock);
}
void knock_reference_isr(void) IRQ_HANDLER(0x12) {
    u32 segment, start, length;
    CC2IE = 0;
    if (boundary_epoch != ecu.rotation.epoch || ecu.authority.inhibits ||
        ecu.rotation.state != ROT_VALID || !knock_config.mode) { hal_knock_cancel(); return; }
    if (phase) { fault(KNOCK_FAULT_WINDOW); return; }
    window = knock_config;
    /* Quantize in OEM T8 /64 units before converting to T7 /16. */
    segment = ecu.rotation.normal * 30UL / 4UL;
    start = (u32)window.start * segment / 240UL * 4UL;
    length = (u32)window.length * segment / 240UL * 4UL;
    if (!start || !length || start > 32767UL || length > 32767UL) {
        fault(KNOCK_FAULT_WINDOW); return;
    }
    sample_type = KNOCK_NORMAL;
    /* Periodic health pairs: one null/test pair per 480 half-turn events. */
    health_count++;
    if (health_count == 479U) sample_type = KNOCK_NULL;
    else if (health_count >= 480U) { sample_type = KNOCK_TEST; health_count = 0; }
    PIN_KNOCK_KSA3 = (bit)(sample_type == KNOCK_NULL);
    window_gain = knock.gain;
    window_length = (u16)length;
    phase = 1;
    CC16IE = 0; CC16 = (u16)(T7 + (u16)start); CC16IR = 0; CC16IE = 1;
}
static void consume(void) {
    u16 reference;
    if (!pending) return;
    pending = 0;
    if (pending_config.generation != ecu.cal.generation ||
        pending_config.epoch != ecu.rotation.epoch ||
        ecu.milliseconds - pending_at > pending_config.stale_ms) {
        if (knock.stale_samples != 65535U) knock.stale_samples++;
        knock_invalidate(KNOCK_FAULT_STALE); return;
    }
    if (knock.bench == 2U) {
        knock_job_result.completed++;
        if (pending_type == KNOCK_NULL) knock_job_result.null_raw = pending_raw;
        if (pending_type == KNOCK_TEST) knock_job_result.test_raw = pending_raw;
        if (pending_type == KNOCK_GAIN_TEST) {
            knock_job_result.gain_raw[pending_gain] = pending_raw;
            return;
        }
    }
    /* Pooling samples globally means a pending sample can predate the most
       recent autorange decision. Refer its noise estimate to its actual gain. */
    reference = knock.reference;
    while (knock.gain < pending_gain) { reference *= 2U; knock.gain++; }
    while (knock.gain > pending_gain) { reference >>= 1; knock.gain--; }
    knock.reference = (u8)(reference > 255U ? 255U : reference);
    knock_sample(&pending_config, pending_raw, pending_type, pending_ticks, pending_at);
}
void knock_window_isr(void) IRQ_HANDLER(0x30) {
    u16 raw, start, deadline, spins;
    if (!valid_window()) { hal_knock_cancel(); return; }
    if (phase == 1U) {
        deadline = (u16)(CC16 + window_length);
        if ((s16)(deadline - T7) <= 0) { fault(KNOCK_FAULT_WINDOW); return; }
        PIN_KNOCK_MF = 1;
        CC16 = deadline; /* close deadline installed before previous-sample work */
        phase = 2;
        if (sample_type == KNOCK_TEST) { gain(3); PIN_KNOCK_KTI = 1; }
        consume();
        if (phase != 2U || !valid_window()) { hal_knock_cancel(); return; }
        if (sample_type == KNOCK_NULL) {
            start = T7; spins = 0;
            while ((u16)(T7 - start) < 25U && ++spins < 1000U) { }
            if ((u16)(T7 - start) < 25U) { fault(KNOCK_FAULT_WINDOW); return; }
            if (!hal_knock_sample(&raw)) { fault(KNOCK_FAULT_ADC); return; }
            if (phase != 2U || !valid_window()) { hal_knock_cancel(); return; }
            knock_null_start(raw);
        }
        if ((s16)(CC16 - T7) <= 0) fault(KNOCK_FAULT_WINDOW);
    } else if (phase == 2U) {
        PIN_KNOCK_MF = 0;
        PIN_KNOCK_KTI = 0;
        CC16IE = 0; phase = 3; /* acquisition still owns window/configuration */
        if (!hal_knock_sample(&raw)) { fault(KNOCK_FAULT_ADC); return; }
        if (phase != 3U || !valid_window()) { hal_knock_cancel(); return; }
        pending_raw = raw; pending_config = window; pending_gain = window_gain;
        pending_at = ecu.milliseconds; pending_type = sample_type;
        pending_ticks = window_length; pending = 1;
        phase = 0;
        gain(knock.gain_code);
        PIN_KNOCK_KSA3 = 0;
    } else hal_knock_cancel();
}
void hal_knock_bench(u8 job, u32 now) {
    u16 lock;
    if (!job) { job_phase = 0; return; }
    if (ecu.rotation.rpm || ecu.control.mode != ENGINE_STOPPED || ecu.cal.staging ||
        ecu.storage.phase || !ecu.service || !ecu.key_input) {
        knock_job_result.state = 4;
        knock.bench = 0; knock_invalidate(0); job_phase = 0; return;
    }
    if (phase || now - bench_at < 20UL) return;
    lock = hal_lock();
    consume();
    if (job == 2U && (job_phase >= 13U || knock.fault)) {
        knock_job_result.state = knock.fault ? 3 : 2;
        knock_job_result.fault = knock.fault;
        knock.bench = 0; hal_knock_cancel(); job_phase = 0;
        hal_unlock(lock); return;
    }
    window = knock_config; window.eligible = 1;
    sample_type = job == 1U ? KNOCK_NORMAL :
        (job_phase >= 6U ? KNOCK_GAIN_TEST : (job_phase & 1U ? KNOCK_TEST : KNOCK_NULL));
    PIN_KNOCK_KSA3 = (bit)(sample_type == KNOCK_NULL);
    window_gain = knock.gain;
    if (sample_type == KNOCK_GAIN_TEST) {
        window_gain = (u8)(job_phase - 6U);
        gain(window.gain_code[window_gain]);
    }
    /* OEM stopped seed: 0x10 delay/0x80 length at fCPU/512.
       Convert to the standalone's fCPU/16 time base. */
    window_length = 4096U;
    phase = 1; CC16IE = 0; CC16 = (u16)(T7 + 512U); CC16IR = 0; CC16IE = 1;
    bench_at = now;
    if (job == 2U) job_phase++;
    hal_unlock(lock);
}
