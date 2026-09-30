#include "knock.h"
#include "lifecycle.h"
#include "oem_runtime.h"
#include "oem_layout.h"
#include <string.h>
KnockState knock;
KnockConfig knock_config;
KnockJob knock_job_result;
static u8 rpm_gate, coolant_gate, load_gate, configured_filter, configured_ready;
static u16 configured_generation;
#include "knock_defaults.inc"
u16 knock_mv(u16 raw) { return (u16)(((u32)raw * 5000UL + 512UL) / 1024UL); }
void knock_defaults(u8 *c) {
    memcpy(c + CAL_KNOCK, knock_default_bytes, sizeof(knock_default_bytes));
}
u8 knock_validate(const u8 *c, u16 *error) {
    u16 i, rpm;
    *error = CAL_KNOCK;
    if (c[CAL_KNOCK_MODE] > KNOCK_CONTROL ||
        (c[CAL_KNOCK_FILTER] != 0U && c[CAL_KNOCK_FILTER] != 16U) ||
        get16(c + CAL_KNOCK_MIN_RPM) < 600U || get16(c + CAL_KNOCK_MIN_RPM) > 6000U ||
        c[CAL_KNOCK_COOLANT] < 5U || !c[CAL_KNOCK_DIVISOR] ||
        c[CAL_KNOCK_GAIN] > 6U || c[CAL_KNOCK_REFERENCE] < 13U ||
        c[CAL_KNOCK_REFERENCE] > 51U || get16(c + CAL_KNOCK_LATCH_MS) > 5000U ||
        !get16(c + CAL_KNOCK_LATCH_MS) || get16(c + CAL_KNOCK_STALE_MS) < 100U ||
        get16(c + CAL_KNOCK_STALE_MS) > 1000U || c[CAL_KNOCK_DRIFT_LIMIT] > 21U ||
        c[CAL_KNOCK_NULL_TOLERANCE] > 25U || !c[CAL_KNOCK_TEST_SHIFT] ||
        !c[CAL_KNOCK_DEBOUNCE] || c[CAL_KNOCK_DEBOUNCE] > 10U) return 0;
    *error = CAL_KNOCK_MANUAL_GAIN;
    if (c[CAL_KNOCK_MANUAL_GAIN] > 1U || !c[CAL_KNOCK_ATTACK_STEP] ||
        c[CAL_KNOCK_RETARD_LIMIT] > 16U ||
        c[CAL_KNOCK_ATTACK_STEP] > c[CAL_KNOCK_RETARD_LIMIT] ||
        get16(c + CAL_KNOCK_RECOVERY_PERCENT) < 25U ||
        get16(c + CAL_KNOCK_RECOVERY_PERCENT) > 400U) return 0;
    for (i = 0; i < 16U; i++) {
        *error = (u16)(CAL_KNOCK_RPM_AXIS + i * 2U);
        rpm = get16(c + *error);
        if (rpm < 400U || rpm > 12000U || (i && rpm <= get16(c + *error - 2U))) return 0;
        *error = (u16)(CAL_KNOCK_START + i);
        if (c[*error] < 2U || c[*error] > 80U || c[CAL_KNOCK_LENGTH + i] < 13U ||
            c[CAL_KNOCK_LENGTH + i] > 80U || c[*error] + c[CAL_KNOCK_LENGTH + i] > 160U ||
            c[CAL_KNOCK_THRESHOLD + i] < 16U || c[CAL_KNOCK_THRESHOLD + i] > 80U ||
            !c[CAL_KNOCK_ATTACK + i] || c[CAL_KNOCK_MAXIMUM + i] > 16U ||
            c[CAL_KNOCK_ATTACK + i] > c[CAL_KNOCK_MAXIMUM + i] ||
            !c[CAL_KNOCK_HOLD + i] || !c[CAL_KNOCK_LOAD + i]) return 0;
    }
    for (i = 0; i < 7U; i++) {
        *error = (u16)(CAL_KNOCK_GAIN_CODES + i);
        /* Code 4 duplicates x16; skip it to retain the doubling ladder. */
        if (c[*error] != (u8)(i < 4U ? i : i + 1U)) return 0;
    }
    *error = 0;
    return 1;
}
void knock_init(void) {
    memset(&knock, 0, sizeof(knock));
    memset(&knock_config, 0, sizeof(knock_config));
    memset(&knock_job_result, 0, sizeof(knock_job_result));
    knock.mv = 65535U;
    knock.null_start = knock.offset = 37;
    /* Cancellation can run before the first calibration snapshot. Keep the
       IC at the OEM released gain even when storage contains no valid tune. */
    knock.gain = 4; knock.gain_code = 5; knock.reference = 51;
    rpm_gate = coolant_gate = load_gate = 0;
    configured_generation = 0;
    configured_ready = 0;
    configured_filter = 255;
}
void knock_invalidate(u8 fault) {
    u16 lock = hal_lock();
    knock.valid = knock.decision = knock.recent = knock.qualified = knock.good_count = 0;
    if (fault) knock.fault = fault;
    /* Retain existing retard across a running failure, including sync loss. */
    hal_knock_cancel();
    hal_unlock(lock);
}
static u8 curve(const u8 *c, u16 base, const AxisAt *at) {
    return (u8)lerp(c[base + at->index], c[base + at->index + 1U], at->fraction);
}
void knock_update(u32 now, const Rotation *r, const u8 *c) {
    KnockConfig cfg;
    AxisAt at;
    u16 lock, load, min_load, coolant;
    u32 filling;
    u8 mode = c[CAL_KNOCK_MODE];
    if (!configured_ready || configured_generation != ecu.cal.generation) {
        knock_invalidate(0);
        lock = hal_lock();
        knock.reference = c[CAL_KNOCK_REFERENCE];
        knock.gain = c[CAL_KNOCK_GAIN];
        knock.gain_code = c[CAL_KNOCK_GAIN_CODES + knock.gain];
        knock.mode = mode;
        if (ecu.control.mode == ENGINE_STOPPED || mode != KNOCK_CONTROL) {
            knock.retard = knock.scheduled = knock.fault = 0;
            knock.hold = 0;
        }
        configured_generation = ecu.cal.generation;
        configured_ready = 1;
        hal_knock_cancel(); /* publish the selected starting gain at the pins */
        hal_unlock(lock);
    }
    if (configured_filter != c[CAL_KNOCK_FILTER] && ecu.control.mode == ENGINE_STOPPED) {
        hal_knock_configure(c[CAL_KNOCK_FILTER]);
        hal_knock_cancel(); /* filter release uses the OEM gain; restore selected gain */
        configured_filter = c[CAL_KNOCK_FILTER];
    }
    memset(&cfg, 0, sizeof(cfg));
    axis_at(c + CAL_KNOCK_RPM_AXIS, 16, (s16)r->rpm, 0, &at);
    cfg.mode = mode; cfg.generation = ecu.cal.generation; cfg.epoch = r->epoch;
    cfg.start = curve(c, CAL_KNOCK_START, &at);
    cfg.length = curve(c, CAL_KNOCK_LENGTH, &at);
    cfg.threshold = curve(c, CAL_KNOCK_THRESHOLD, &at);
    cfg.attack = c[CAL_KNOCK_ATTACK_STEP];
    cfg.maximum = c[CAL_KNOCK_RETARD_LIMIT];
    cfg.hold = (u16)((u32)curve(c, CAL_KNOCK_HOLD, &at) * 400UL /
                    get16(c + CAL_KNOCK_RECOVERY_PERCENT));
    if (!cfg.hold) cfg.hold = 1;
    cfg.manual_gain = c[CAL_KNOCK_MANUAL_GAIN];
    cfg.divisor = c[CAL_KNOCK_DIVISOR];
    cfg.initial_gain = c[CAL_KNOCK_GAIN]; cfg.initial_reference = c[CAL_KNOCK_REFERENCE];
    memcpy(cfg.gain_code, c + CAL_KNOCK_GAIN_CODES, 7);
    cfg.latch_ms = get16(c + CAL_KNOCK_LATCH_MS);
    cfg.stale_ms = get16(c + CAL_KNOCK_STALE_MS);
    cfg.drift_limit = c[CAL_KNOCK_DRIFT_LIMIT]; cfg.null_tolerance = c[CAL_KNOCK_NULL_TOLERANCE];
    cfg.test_shift = c[CAL_KNOCK_TEST_SHIFT]; cfg.debounce = c[CAL_KNOCK_DEBOUNCE];
    coolant = oem_runtime.state.coolant.value[OEM_CLT_950E];
    coolant_gate = (u8)(oem_runtime.initialized && ecu.sensors.clt.quality == QUALITY_VALID &&
        (coolant >= c[CAL_KNOCK_COOLANT] ||
         (coolant_gate && coolant > c[CAL_KNOCK_COOLANT] - 5U)));
    rpm_gate = (u8)(r->rpm >= get16(c + CAL_KNOCK_MIN_RPM) ||
                   (rpm_gate && r->rpm > get16(c + CAL_KNOCK_MIN_RPM) - 120U));
    /* Standalone filling estimate: VE * MAP/100 kPa * 273.15 K / IAT(K).
       Convert percent to OEM 0.75% units. MAP is required even in alpha-N. */
    filling = 0;
    if (ecu.sensors.map.quality == QUALITY_VALID && ecu.sensors.map.value > 0 &&
        ecu.sensors.iat.quality == QUALITY_VALID && ecu.sensors.iat.value > -100 &&
        ecu.sensors.iat.value < 300) {
        filling = scale32((u32)ecu.control.ve * (u16)ecu.sensors.map.value, 27315U,
                         (u16)(27315L + (s32)ecu.sensors.iat.value * 100L));
        filling = filling > 19125UL ? 255UL : filling * 4UL / 300UL;
    }
    load = (u16)(filling > 255UL ? 255U : filling);
    min_load = curve(c, CAL_KNOCK_LOAD, &at);
    load_gate = (u8)(load >= min_load || (load_gate && load + 4U > min_load));
    cfg.eligible = (u8)(mode && r->state == ROT_VALID && ecu.control.mode == ENGINE_RUNNING &&
        !ecu.authority.inhibits && !ecu.service && !ecu.authority.plan.fuel_cut &&
        !ecu.authority.plan.spark_cut && !ecu.authority.plan.soft_fuel &&
        !ecu.authority.plan.soft_spark && rpm_gate && coolant_gate && load_gate &&
        ecu.sensors.map.quality == QUALITY_VALID && ecu.sensors.iat.quality == QUALITY_VALID);
    lock = hal_lock();
    if (r->epoch != knock_config.epoch ||
        (!knock.bench && (!mode || ecu.service || ecu.authority.inhibits)))
        knock_invalidate(0);
    knock_config = cfg;
    if (knock.valid && now - knock.stamp > cfg.stale_ms) {
        if (knock.stale_samples != 65535U) knock.stale_samples++;
        knock_invalidate(KNOCK_FAULT_STALE);
    }
    if (ecu.control.mode == ENGINE_STOPPED && !knock.bench) {
        knock.retard = knock.scheduled = 0;
        knock.fault = knock.qualified = knock.good_count = knock.rail_count = 0;
        knock.null_bad = knock.test_bad = knock.drift_bad = 0;
        knock.last_normal = knock.decision = knock.recent = 0;
        if (ecu.adc[15].seen && now - ecu.adc[15].stamp <= get16(c + CAL_SENSOR_AGE)) {
            knock.raw = ecu.adc[15].raw; knock.mv = knock_mv(knock.raw);
            knock.stamp = ecu.adc[15].stamp; knock.source = 1; knock.valid = 1;
        }
    }
    hal_unlock(lock);
    hal_knock_bench(knock.bench, now);
}
/* Standard OEM detector path. Alternate flags, trim and per-slot ceilings
   are intentionally omitted. OEM stores the low byte of the divided ratio;
   the overload branch remains independent of this wrap. */
void knock_detect(KnockState *s, const KnockConfig *cfg, u8 raw) {
    u16 ratio, candidate, ceiling;
    s16 step, reference;
    s->amplitude = raw > s->offset ? (u8)(raw - s->offset) : 0;
    s->decision = 0;
    if (!s->reference || !cfg->divisor) { s->fault = KNOCK_FAULT_REFERENCE; return; }
    ceiling = s->gain ? 255U : 240U; /* Both OEM ceiling curves are 0xF0. */
    ratio = (u16)s->amplitude * 16U / (s->reference < ceiling ? s->reference : ceiling);
    s->ratio = (u8)ratio;
    s->threshold = cfg->threshold;
    s->decision = (u8)(cfg->eligible && (s->ratio >= cfg->threshold ||
                       (raw > s->null_start && raw - s->null_start > 189U)));
    if (s->decision) candidate = (u16)s->amplitude * 16U / cfg->threshold;
    else if (cfg->eligible && s->ratio >= 40U) candidate = s->amplitude >> 1;
    else candidate = s->amplitude;
    step = (s16)((s16)candidate - s->reference) / cfg->divisor;
    if (!step && candidate != s->reference) step = candidate > s->reference ? 1 : -1;
    reference = (s16)s->reference + step;
    if (s->gain == 6U && reference <= 13) reference = 13;
    if (!cfg->manual_gain) {
        if (reference >= 51 && s->gain) { s->gain--; reference >>= 1; }
        else if (reference <= 16 && s->gain < 6U) { s->gain++; reference *= 2; }
    }
    s->reference = (u8)clamp32(reference, 1, 255);
    s->gain_code = cfg->gain_code[s->gain];
}
void knock_null_start(u16 raw) { knock.null_start = (u8)(raw >> 2); }
void knock_sample(const KnockConfig *cfg, u16 raw, u8 type, u16 ticks, u32 now) {
    s32 drift;
    u16 lock = hal_lock(), amount;
    u8 v = (u8)(raw >> 2), bad;
    if (cfg->generation != ecu.cal.generation || cfg->epoch != ecu.rotation.epoch || raw > 1023U) {
        knock_invalidate(KNOCK_FAULT_STALE); hal_unlock(lock); return;
    }
    if (!knock.bench && (ecu.service || ecu.authority.inhibits ||
        ecu.rotation.state != ROT_VALID || ecu.control.mode != ENGINE_RUNNING ||
        ecu.authority.plan.fuel_cut || ecu.authority.plan.spark_cut ||
        ecu.authority.plan.soft_fuel || ecu.authority.plan.soft_spark)) {
        knock_invalidate(0); hal_unlock(lock); return;
    }
    if (type == KNOCK_NULL) {
        knock.null_count++;
        bad = (u8)(knock.null_start > 37U + cfg->null_tolerance ||
                   knock.null_start + cfg->null_tolerance < 37U);
        if (bad) { if (knock.null_bad < cfg->debounce) knock.null_bad++; }
        else knock.null_bad = 0;
        if (knock.null_bad >= cfg->debounce) knock_invalidate(KNOCK_FAULT_NULL);
        if (ticks && !bad && ecu.rotation.rpm < 5000U) {
            drift = ((s32)v - knock.null_start) * 65536L / ticks;
            knock.drift_q16 = (s16)clamp32(drift, -32768L, 32767L);
            /* OEM drift limit 2150 uses fCPU/64 window ticks; our ticks
               are fCPU/16, so compare four times the measured rate. */
            if (knock.bench || ecu.rotation.rpm > 2000U) {
                if (drift * 4L > 2150L || drift * 4L < -2150L) {
                    if (knock.drift_bad < cfg->debounce) knock.drift_bad++;
                } else knock.drift_bad = 0;
                if (knock.drift_bad >= cfg->debounce) knock_invalidate(KNOCK_FAULT_DRIFT);
            }
        }
        hal_unlock(lock); return;
    }
    if (type == KNOCK_TEST) {
        knock.test_count++;
        bad = (u8)(v < knock.null_start || v - knock.null_start < cfg->test_shift);
        if (bad) { if (knock.test_bad < cfg->debounce) knock.test_bad++; }
        else knock.test_bad = 0;
        if (knock.test_bad >= cfg->debounce) knock_invalidate(KNOCK_FAULT_TEST);
        hal_unlock(lock); return;
    }
    if (type != KNOCK_NORMAL) { hal_unlock(lock); return; }
    knock.type = KNOCK_NORMAL;
    knock.raw = raw; knock.mv = knock_mv(raw); knock.stamp = now;
    knock.source = knock.bench ? 3 : 2; knock.valid = 1;
    knock.sequence++; knock.normal_count++;
    knock.generation = cfg->generation; knock.epoch = cfg->epoch;
    knock.window_ticks = ticks;
    knock.last_normal = (u8)!knock.bench;
    /* Persistent rails are a health fault; a single high valid integral may
       still assert the OEM overload decision. */
    if (raw == 0 || raw == 1023U) { if (knock.rail_count < cfg->debounce) knock.rail_count++; }
    else knock.rail_count = 0;
    if (knock.rail_count >= cfg->debounce) knock_invalidate(KNOCK_FAULT_RAIL);
    if (knock.fault) {
        knock.valid = knock.decision = 0;
        hal_unlock(lock); return;
    } /* stopped reset/requalification required */
    if (knock.good_count < cfg->debounce) knock.good_count++;
    knock.qualified = (u8)(knock.good_count >= cfg->debounce);
    drift = (s32)knock.drift_q16 * ticks;
    /* Signed multiply-high uses floor, including negative products. */
    drift = drift >= 0 ? drift / 65536L : -((-drift + 65535L) / 65536L);
    knock.offset = (u8)clamp32(clamp32(knock.null_start, 25, 48) +
                              clamp32(drift, -(s32)cfg->drift_limit, cfg->drift_limit), 0, 255);
    knock_detect(&knock, cfg, v);
    if (knock.decision) {
        knock.count++; knock.last_knock = now; knock.recent = 1;
        if (cfg->mode == KNOCK_CONTROL && !knock.bench && knock.qualified) {
            amount = (u16)knock.retard + cfg->attack;
            if (knock.retard == cfg->maximum && knock.ceiling_events != 65535U) knock.ceiling_events++;
            knock.retard = (u8)(amount > cfg->maximum ? cfg->maximum : amount);
            knock.hold = cfg->hold;
        }
    } else if (cfg->eligible && cfg->mode == KNOCK_CONTROL && knock.qualified && !knock.bench) {
        if (knock.hold) knock.hold--;
        if (!knock.hold && knock.retard) { knock.retard--; knock.hold = cfg->hold; }
    }
    hal_unlock(lock);
}
s16 knock_advance(s16 base) {
    u16 lock = hal_lock();
    s16 clipped = (s16)clamp32(base, -128, 127), result;
    u8 retard = knock.mode == KNOCK_CONTROL ? knock.retard : 0;
    result = (s16)clamp32((s32)base - retard, -128, 127);
    knock.scheduled = (u8)(clipped > result ? clipped - result : 0);
    hal_unlock(lock);
    return result;
}
void knock_monitor(u8 *out, u32 now) {
    u16 lock = hal_lock();
    u8 fresh = (u8)(knock.valid && now - knock.stamp <= knock_config.stale_ms);
    put16(out, knock.mv);
    out[2] = (u8)((knock.recent && now - knock.last_knock < knock_config.latch_ms ? 1U : 0U) |
        (fresh ? 2U : 0U) | (fresh && knock.mode == KNOCK_CONTROL && knock.qualified && !knock.fault &&
         knock_config.eligible && !knock.bench ? 4U : 0U) |
        (knock.mode == KNOCK_MONITOR ? 8U : 0U) | (knock.fault ? 16U : 0U) |
        (knock.bench ? 32U : 0U) | (knock.decision && fresh ? 64U : 0U) |
        (knock.last_normal && fresh ? 128U : 0U));
    out[3] = knock.retard;
    hal_unlock(lock);
}
void knock_details(u8 *out, u32 now) {
    u16 lock = hal_lock();
    memset(out, 0, 64);
    out[0] = 1; knock_monitor(out + 1, now);
    put16(out + 5, knock.raw); put16(out + 7, knock.sequence);
    put32(out + 9, knock.stamp); put32(out + 13, now - knock.stamp);
    put32(out + 17, knock.count); put32(out + 21, knock.recent ? now - knock.last_knock : 0xFFFFFFFFUL);
    out[25] = knock.source; out[26] = knock.type; out[27] = knock.offset;
    out[28] = knock.amplitude; out[29] = knock.reference; out[30] = knock.gain;
    out[31] = knock.gain_code; out[32] = knock.ratio; out[33] = knock.threshold;
    out[34] = knock.decision; out[35] = knock.scheduled; out[36] = knock.fault;
    put16(out + 37, knock.hold); put16(out + 39, knock.epoch); put16(out + 41, knock.generation);
    put16(out + 43, knock.adc_timeouts); put16(out + 45, knock.missed_windows);
    put16(out + 47, knock.stale_samples); put16(out + 49, knock.ceiling_events);
    put32(out + 51, knock.normal_count); put32(out + 55, knock.null_count);
    put32(out + 59, knock.test_count); out[63] = knock.null_start;
    hal_unlock(lock);
}
u8 knock_job(u8 command, u32 now) {
    u16 lock;
    if (!command) {
        lock = hal_lock(); knock.bench = 0; knock_job_result.state = 4;
        knock_invalidate(0); hal_unlock(lock); return 1;
    }
    if (command > 2U || knock.bench || !ecu.cal.valid || !ecu.key_input || ecu.cal.staging ||
        ecu.storage.phase || ecu.control.mode != ENGINE_STOPPED || ecu.rotation.rpm ||
        (ecu.rotation.seen && hal_capture_clock() - ecu.rotation.last < 312500UL)) return 0;
    if (!service_enter(now)) return 0;
    lock = hal_lock();
    knock_invalidate(0); knock.bench = command; knock.fault = 0;
    memset(&knock_job_result, 0, sizeof(knock_job_result));
    knock_job_result.state = 1;
    knock.null_bad = knock.test_bad = knock.drift_bad = 0;
    hal_unlock(lock); return 1;
}
void knock_job_status(u8 *out, u32 now) {
    u16 lock = hal_lock();
    u8 i;
    knock_details(out, now);
    out[64] = 1; out[65] = knock_job_result.state;
    out[66] = knock_job_result.completed; out[67] = knock_job_result.fault;
    put16(out + 68, knock_job_result.null_raw); put16(out + 70, knock_job_result.test_raw);
    for (i = 0; i < 7; i++) put16(out + 72 + i * 2U, knock_job_result.gain_raw[i]);
    hal_unlock(lock);
}
