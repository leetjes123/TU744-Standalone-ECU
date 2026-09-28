#include "diagnostic_monitors.h"
#include <string.h>
PhaseObservation phase_observation;
MisfireObservation misfire_observation;

static void ingest(DiagnosticMonitors *m, OemDtcState *events, const u8 *cal,
                   u8 event, u8 verdict, u8 subtype);

void diagnostic_phase_arm(u32 stamp, u16 epoch, u8 tooth) SHARED {
    if (phase_observation.armed)
        phase_observation.missed = 1;
    phase_observation.armed_at = stamp;
    phase_observation.armed_ms = ecu.milliseconds;
    phase_observation.rotation_epoch = epoch;
    phase_observation.tooth = tooth;
    phase_observation.captured = 0;
    phase_observation.armed = 1;
}

void diagnostic_phase_capture(u32 stamp) SHARED {
    if (!phase_observation.armed)
        return;
    phase_observation.captured_at = stamp;
    phase_observation.delay = stamp - phase_observation.armed_at;
    phase_observation.captured = 1;
    phase_observation.armed = 0;
}

u8 diagnostic_edge_eligible(void) SHARED {
    return (u8)(ecu.control.mode == ENGINE_RUNNING &&
        !ecu.authority.inhibits && !ecu.authority.plan.fuel_cut &&
        !ecu.authority.plan.spark_cut && !ecu.authority.plan.soft_fuel &&
        !ecu.authority.plan.soft_spark && !ecu.control.dfco);
}
/* The effect of diagnostic_rotation_edge for a valid non-boundary tooth. */
void diagnostic_rotation_ineligible(void) SHARED {
    misfire_observation.boundary_eligible = 0;
}
void diagnostic_rotation_edge(u32 stamp, u16 epoch, u8 tooth, u8 gap,
                              u8 valid) SHARED {
    u8 next, eligible = diagnostic_edge_eligible();
    if (!valid) {
        misfire_observation.seeded = 0;
        return;
    }
    if (!eligible)
        misfire_observation.boundary_eligible = 0;
    /* Both boundaries are present teeth, exactly 180 degrees apart. Never
       seed at an arbitrary first synchronized tooth (a partial window). */
    if (tooth != 30U && !gap)
        return;
    if (!misfire_observation.seeded) {
        misfire_observation.boundary = stamp;
        misfire_observation.seeded = 1;
        misfire_observation.boundary_eligible = eligible;
        return;
    }
    next = (u8)((misfire_observation.head + 1U) & (MISFIRE_QUEUE_SIZE - 1U));
    if (next == misfire_observation.tail) {
        misfire_observation.overflow = 1;
        misfire_observation.tail =
            (u8)((misfire_observation.tail + 1U) & (MISFIRE_QUEUE_SIZE - 1U));
    }
    misfire_observation.duration[misfire_observation.head] =
        stamp - misfire_observation.boundary;
    misfire_observation.epoch[misfire_observation.head] = epoch;
    misfire_observation.slot[misfire_observation.head] = gap ? 1U : 0U;
    misfire_observation.at_ms[misfire_observation.head] = ecu.milliseconds;
    misfire_observation.eligible[misfire_observation.head] =
        (u8)(eligible && misfire_observation.boundary_eligible);
    misfire_observation.phase[misfire_observation.head] =
        misfire_observation.phase_epoch == epoch ? misfire_observation.phase_identity : 0U;
    misfire_observation.head = next;
    misfire_observation.boundary = stamp;
    misfire_observation.boundary_eligible = eligible;
}

static u16 adc_raw(u8 channel, u8 *seen) {
    u16 lock = hal_lock(), raw = ecu.adc[channel].raw;
    *seen = ecu.adc[channel].seen;
    hal_unlock(lock);
    return raw;
}

u8 diagnostic_monitor_enabled(const u8 *cal, u8 event) {
    if (event >= 107U)
        return 0;
    return (u8)((cal[CAL_DTC_ENABLE + event / 8U] >> (event & 7U)) & 1U);
}

u8 diagnostic_monitor_subtype_enabled(const u8 *cal, u8 event, u8 subtype) {
    u8 mask, selector;
    if (event >= 107U)
        return 0;
    mask = cal[CAL_DTC_SUBTYPE_ENABLE + event] & 0x0FU;
    if (!mask)
        mask = 0x0FU; /* schema-4 images predating subtype masks */
    selector = subtype == 1U ? 1U : subtype == 2U ? 2U :
               subtype == 4U ? 4U : subtype == 8U ? 8U : 0U;
    return (u8)(selector && (mask & selector));
}

void diagnostic_monitors_init(DiagnosticMonitors *m) {
    u16 lock;
    memset(m, 0, sizeof(*m));
    lock = hal_lock();
    memset(&phase_observation, 0, sizeof(phase_observation));
    memset(&misfire_observation, 0, sizeof(misfire_observation));
    hal_unlock(lock);
    m->initialized = 1;
}

static void phase_events(DiagnosticMonitors *m, OemDtcState *events,
                         const u8 *cal, u32 now) {
    u16 lock, epoch, minimum, maximum, delta_limit;
    u8 captured, armed, missed;
    u32 armed_ms, delay;
    Rotation rotation;
    rotation_snapshot(&rotation);
    if (m->phase_epoch != rotation.epoch) {
        m->phase_valid = m->phase_seeded = 0;
        m->phase_epoch = rotation.epoch;
    }
    lock = hal_lock();
    captured = phase_observation.captured;
    armed = phase_observation.armed;
    missed = phase_observation.missed;
    delay = phase_observation.delay;
    epoch = phase_observation.rotation_epoch;
    armed_ms = phase_observation.armed_ms;
    phase_observation.captured = 0;
    phase_observation.missed = 0;
    hal_unlock(lock);
    minimum = get16(cal + CAL_DTC_PHASE_MIN_TICKS);
    maximum = get16(cal + CAL_DTC_PHASE_MAX_TICKS);
    delta_limit = get16(cal + CAL_DTC_PHASE_DELTA_TICKS);
    if (!minimum) minimum = 66U;
    if (!maximum) maximum = 168U;
    if (!delta_limit) delta_limit = 18U;
    if (captured) {
        if (epoch != rotation.epoch || rotation.state != ROT_VALID ||
            delay < minimum || delay > maximum) {
            m->phase_valid = m->phase_seeded = 0;
            ingest(m, events, cal, 0x4BU, MONITOR_FAIL, 1U);
        } else {
            if (m->phase_seeded &&
                (delay > m->phase_last_delay ? delay - m->phase_last_delay :
                                               m->phase_last_delay - delay) >=
                    delta_limit) {
                /* On this DEPHIA strategy the direction of the breakdown-delay
                   step carries the 1/4 attribution. Polarity requires a board
                   waveform acceptance test before release. */
                /* The ROM publishes phase index 3 for an increasing delay and
                   1 for a decreasing delay. Board/wiring polarity decides
                   which identity is cylinder 1, so retain it as calibration. */
                m->phase_cylinder_one =
                    (u8)((delay > m->phase_last_delay) ^
                         (cal[CAL_DTC_PHASE_POLARITY] & 1U));
                m->phase_valid = 1;
            }
            m->phase_last_delay = (u16)delay; /* narrowed only after full-width validation */
            m->phase_last_at = now;
            m->phase_seeded = 1;
            ingest(m, events, cal, 0x1AU, MONITOR_PASS, 0);
            ingest(m, events, cal, 0x4BU,
                   m->phase_valid ? MONITOR_PASS : MONITOR_UNKNOWN, 0);
        }
    } else if (missed) {
        m->phase_valid = m->phase_seeded = 0;
        ingest(m, events, cal, 0x1AU, MONITOR_FAIL, 1U);
    } else if (ecu.control.mode == ENGINE_RUNNING && armed &&
               now - armed_ms > (get16(cal + CAL_DTC_PHASE_TIMEOUT_MS) ?
                                  get16(cal + CAL_DTC_PHASE_TIMEOUT_MS) : 2000U)) {
        hal_phase_disarm();
        m->phase_valid = m->phase_seeded = 0;
        ingest(m, events, cal, 0x1AU, MONITOR_FAIL, 1U);
    } else if (ecu.control.mode != ENGINE_RUNNING) {
        m->phase_valid = m->phase_seeded = 0;
        ingest(m, events, cal, 0x1AU, MONITOR_UNKNOWN, 0);
        ingest(m, events, cal, 0x4BU, MONITOR_UNKNOWN, 0);
    }
    if (m->phase_seeded && now - m->phase_last_at >
        (get16(cal + CAL_DTC_PHASE_TIMEOUT_MS) ? get16(cal + CAL_DTC_PHASE_TIMEOUT_MS) : 2000U))
        m->phase_valid = m->phase_seeded = 0;
    lock = hal_lock();
    misfire_observation.phase_epoch = rotation.epoch;
    misfire_observation.phase_identity = m->phase_valid ?
        (u8)(m->phase_cylinder_one ? 2U : 1U) : 0U;
    hal_unlock(lock);
}

static void ingest(DiagnosticMonitors *m, OemDtcState *events, const u8 *cal,
                   u8 event, u8 verdict, u8 subtype) {
    u8 fail_limit, pass_limit;
    u16 descriptor;
    if (event >= 107U)
        return;
    if (!diagnostic_monitor_enabled(cal, event)) {
        ecu.diagnostics.support[event] = 0;
        m->fail[event] = m->pass[event] = 0;
        if (m->enabled[event] && (events->live[event] & 1U)) {
            descriptor = 2U;
            oem_dtc_ingest(events, event, &descriptor);
        }
        m->enabled[event] = 0;
        return;
    }
    m->enabled[event] = 1;
    ecu.diagnostics.support[event] = 1;
    if (verdict == MONITOR_UNKNOWN)
        return;
    if (verdict == MONITOR_FAIL &&
        !diagnostic_monitor_subtype_enabled(cal, event, subtype)) {
        descriptor = 2U;
        m->fail[event] = m->pass[event] = 0;
        if (events->live[event] & 1U)
            oem_dtc_ingest(events, event, &descriptor);
        return;
    }
    fail_limit = event >= 0x03U && event <= 0x0CU
                     ? 1U /* occurrence qualification is over a complete crank window */
                     : (cal[CAL_DTC_FAIL_COUNT] ? cal[CAL_DTC_FAIL_COUNT] : 3U);
    pass_limit = cal[CAL_DTC_PASS_COUNT] ? cal[CAL_DTC_PASS_COUNT] : 3U;
    if (verdict == MONITOR_FAIL) {
        m->pass[event] = 0;
        if (m->fail[event] < fail_limit)
            m->fail[event]++;
        if (m->fail[event] < fail_limit)
            return;
        descriptor = (u16)(((u16)subtype << 8) | 3U);
    } else {
        m->fail[event] = 0;
        if (m->pass[event] < pass_limit)
            m->pass[event]++;
        if (m->pass[event] < pass_limit)
            return;
        descriptor = 2U;
    }
    m->completed[event / 8U] |= (u8)(1U << (event & 7U));
    /* Native callback selection consumes the producer's allocation bit.
       Dropping it turns every retained failure into NEW instead of COMPLETE,
       leaving record/live completion inconsistent after an ignition reset. */
    descriptor |= events->live[event] & 0x1000U;
    oem_dtc_ingest(events, event, &descriptor);
}

static void mode_unavailable(DiagnosticMonitors *m, OemDtcState *events,
                             u8 event) {
    u16 descriptor = 2U;
    ecu.diagnostics.support[event] = 0;
    m->fail[event] = m->pass[event] = 0;
    if (events->live[event] & 1U)
        oem_dtc_ingest(events, event, &descriptor);
}

static void sensor_event(DiagnosticMonitors *m, OemDtcState *events, const u8 *cal,
                         u8 event, u8 channel, u8 quality, u8 performance) {
    u8 seen, subtype;
    u16 raw = adc_raw(channel, &seen);
    if (quality == QUALITY_VALID) {
        ingest(m, events, cal, event, performance ? MONITOR_FAIL : MONITOR_PASS,
               performance ? 8U : 0U);
        return;
    }
    if (quality == QUALITY_CONFIG)
        subtype = 4U;
    else if (!seen || quality == QUALITY_STALE)
        subtype = event == 0x43U ? 4U : 4U;
    else
        subtype = raw >= 512U ? 1U : 2U;
    ingest(m, events, cal, event, MONITOR_FAIL, subtype);
}

static void voltage_event(DiagnosticMonitors *m, OemDtcState *events, const u8 *cal,
                          u8 performance) {
    u8 seen;
    u16 raw = adc_raw(5U, &seen);
    u8 subtype;
    if (ecu.sensors.battery.quality == QUALITY_VALID) {
        ingest(m, events, cal, 0x65U,
               performance ? MONITOR_FAIL : MONITOR_PASS,
               performance ? 8U : 0U);
        return;
    }
    if (!seen || ecu.sensors.battery.quality == QUALITY_STALE)
        subtype = 4U;
    else
        subtype = raw >= 512U ? 1U : 2U;
    ingest(m, events, cal, 0x65U, MONITOR_FAIL, subtype);
}

static u8 excessive_slew(s16 current, s16 previous, u16 per_second,
                         u32 elapsed) {
    u32 difference = current >= previous ? (u16)(current - previous) :
                                           (u16)(previous - current);
    if (!elapsed || elapsed > 1000UL)
        return 0;
    return (u8)(difference * 1000UL > (u32)per_second * elapsed);
}

static void vss_event(DiagnosticMonitors *m, OemDtcState *events, const u8 *cal) {
    u16 speed = ecu.sensors.speed_kph;
    u16 lock = hal_lock();
    u32 edge = ecu.vss_edge_stamp, now = ecu.milliseconds;
    u8 max = cal[CAL_DTC_VSS_MAX_KPH] ? cal[CAL_DTC_VSS_MAX_KPH] : 250U;
    u8 verdict = MONITOR_PASS, subtype = 0;
    hal_unlock(lock);
    if (!ecu.key_input) {
        m->vss_moving = m->vss_lost = 0;
    } else if (ecu.sensors.vss_valid) {
        m->vss_lost = 0;
        m->vss_moving = (u8)(speed > 20U);
        m->vss_moving_at = now;
    }
    if (m->vss_moving && !ecu.sensors.vss_valid && now - edge > 1500UL &&
        now - m->vss_moving_at <= 5000UL)
        m->vss_lost = 1;
    if (now - m->vss_moving_at > 5000UL)
        m->vss_moving = 0;
    if (speed > max) {
        verdict = MONITOR_FAIL;
        subtype = 1U;
    } else if (m->vss_lost) {
        verdict = MONITOR_FAIL;
        subtype = 2U;
    } else if (speed > m->previous_speed + 80U) {
        verdict = MONITOR_FAIL;
        subtype = 8U;
    } else if (!ecu.sensors.vss_valid && !m->previous_speed)
        verdict = MONITOR_UNKNOWN; /* Stationary and missing pulses are identical. */
    ingest(m, events, cal, 0x68U, verdict, subtype);
    m->previous_speed = speed;
}

static void crank_events(DiagnosticMonitors *m, OemDtcState *events, const u8 *cal) {
    Rotation r;
    u8 loss, epoch_change, failed;
    rotation_snapshot(&r); /* including the 32-bit event counter */
    loss = (u8)(m->rotation_seeded && r.losses != m->previous_losses);
    epoch_change = (u8)(m->rotation_seeded && r.epoch != m->previous_rotation_epoch);
    if (!ecu.key_input) {
        m->crank_pending = m->crank_was_valid = 0;
        ingest(m, events, cal, 0x0DU, MONITOR_UNKNOWN, 0);
        ingest(m, events, cal, 0x4AU, MONITOR_UNKNOWN, 0);
    } else {
        if (loss || (epoch_change && m->crank_was_valid && r.state != ROT_VALID))
            m->crank_pending = cal[CAL_DTC_FAIL_COUNT] ? cal[CAL_DTC_FAIL_COUNT] : 3U;
        failed = (u8)(m->crank_pending || (m->crank_was_valid && r.state != ROT_VALID));
        ingest(m, events, cal, 0x0DU,
               failed ? MONITOR_FAIL : (r.state == ROT_VALID ? MONITOR_PASS : MONITOR_UNKNOWN),
               4U);
        ingest(m, events, cal, 0x4AU,
               failed ? MONITOR_FAIL : (r.state == ROT_VALID ? MONITOR_PASS : MONITOR_UNKNOWN), 8U);
        if (m->crank_pending)
            m->crank_pending--;
        if (r.state == ROT_VALID)
            m->crank_was_valid = 1;
    }
    m->previous_losses = r.losses;
    m->previous_rotation_epoch = r.epoch;
    m->rotation_seeded = 1;
}

static void misfire_unknown(DiagnosticMonitors *m, OemDtcState *events,
                            const u8 *cal) {
    u8 event;
    for (event = 0x03U; event <= 0x0CU; event++)
        ingest(m, events, cal, event, MONITOR_UNKNOWN, 0U);
}

static void misfire_reset(DiagnosticMonitors *m) {
    m->misfire_seeded = m->misfire_samples = m->misfire_unattributed = 0;
    m->misfire_baseline[0] = m->misfire_baseline[1] = 0;
    memset(m->misfire_hits, 0, sizeof(m->misfire_hits));
}

static void misfire_events(DiagnosticMonitors *m, OemDtcState *events,
                           const u8 *cal) {
    static const u8 cylinder_a[2][2] = {{0x04U, 0x06U}, {0x05U, 0x07U}};
    u32 duration, baseline, center, at, observed_now;
    u16 epoch, lock;
    u8 slot, tail, pct, fail_a, fail_b, cylinder, phase, qualified, event, threshold;
    u8 eligible = (u8)(ecu.control.mode == ENGINE_RUNNING &&
                       ecu.rotation.state == ROT_VALID &&
                       !ecu.authority.inhibits &&
                       !ecu.control.dfco && !ecu.authority.plan.fuel_cut &&
                       !ecu.authority.plan.spark_cut &&
                       !ecu.authority.plan.soft_fuel && !ecu.authority.plan.soft_spark &&
                       ecu.rotation.rpm >= 500U && ecu.rotation.rpm <= 7000U &&
                       ecu.control.tps_rate > -80 && ecu.control.tps_rate < 80);
    m->misfire_qualified = 0;
    lock = hal_lock();
    if (!eligible || misfire_observation.overflow) {
        misfire_observation.tail = misfire_observation.head;
        misfire_observation.overflow = 0;
        misfire_observation.seeded = 0;
        eligible = 0;
    }
    hal_unlock(lock);
    if (!eligible) {
        misfire_reset(m);
        misfire_unknown(m, events, cal);
        return;
    }
    for (;;) {
        lock = hal_lock();
        tail = misfire_observation.tail;
        if (tail == misfire_observation.head) {
            hal_unlock(lock);
            break;
        }
        duration = misfire_observation.duration[tail];
        epoch = misfire_observation.epoch[tail];
        slot = misfire_observation.slot[tail];
        phase = misfire_observation.phase[tail];
        qualified = misfire_observation.eligible[tail];
        at = misfire_observation.at_ms[tail];
        observed_now = ecu.milliseconds; /* coherent with the ISR queue snapshot */
        misfire_observation.tail = (u8)((tail + 1U) & (MISFIRE_QUEUE_SIZE - 1U));
        hal_unlock(lock);
        if (epoch != ecu.rotation.epoch || !qualified || !duration ||
            duration > 250000UL || observed_now - at > 250UL || slot > 1U || phase > 2U) {
            misfire_reset(m);
            continue;
        }
        if (epoch != m->misfire_epoch ||
            (m->misfire_seeded && slot == m->misfire_slot[1]))
            misfire_reset(m);
        m->misfire_epoch = epoch;
        if (m->misfire_seeded >= 2U) {
            /* Compare the middle 180-degree interval with its two neighbors.
               A uniform speed change or linear period ramp is common mode,
               not a frozen learned baseline. This is a standalone imbalance
               estimator, not a claim of production-qualified OEM misfire parity. */
            baseline = (m->misfire_baseline[0] + duration) / 2UL;
            center = m->misfire_baseline[1];
            pct = cal[CAL_DTC_MISFIRE_PERCENT] ? cal[CAL_DTC_MISFIRE_PERCENT] : 35U;
            fail_a = (u8)(center > baseline + baseline * pct / 100UL);
            fail_b = (u8)(center > baseline + baseline * (u16)(pct * 2U) / 100UL);
            m->misfire_hits[0] += fail_a;
            m->misfire_hits[5] += fail_b;
            if (m->misfire_phase[1]) {
                cylinder = cylinder_a[m->misfire_slot[1]][m->misfire_phase[1] == 2U ? 0U : 1U];
                m->misfire_hits[cylinder - 3U] += fail_a;
                m->misfire_hits[cylinder + 2U] += fail_b;
            } else
                m->misfire_unattributed = 1;
            if (++m->misfire_samples >= MISFIRE_WINDOW_SAMPLES) {
                threshold = cal[CAL_DTC_MISFIRE_COUNT] ? cal[CAL_DTC_MISFIRE_COUNT] : 4U;
                for (event = 3U; event <= 12U; event++)
                    ingest(m, events, cal, event,
                        (event != 3U && event != 8U && m->misfire_unattributed) ? MONITOR_UNKNOWN :
                        (m->misfire_hits[event - 3U] >= threshold ? MONITOR_FAIL : MONITOR_PASS), 1U);
                m->misfire_samples = m->misfire_unattributed = 0;
                memset(m->misfire_hits, 0, sizeof(m->misfire_hits));
                m->misfire_qualified = 1;
            }
        }
        m->misfire_baseline[0] = m->misfire_baseline[1];
        m->misfire_baseline[1] = duration;
        m->misfire_slot[0] = m->misfire_slot[1];
        m->misfire_slot[1] = slot;
        m->misfire_phase[0] = m->misfire_phase[1];
        m->misfire_phase[1] = phase;
        if (m->misfire_seeded < 2U)
            m->misfire_seeded++;
    }
}

static void trim_event(DiagnosticMonitors *m, OemDtcState *events, const u8 *cal, u32 now) {
    u32 limit_ms = get16(cal + CAL_DTC_TRIM_MS);
    u16 low = (u16)((u16)cal[0x732] * 8U);
    u16 high = (u16)((u16)cal[0x731] * 8U);
    u8 at_high, at_low, subtype;
    if (!limit_ms)
        limit_ms = 10000UL;
    if (!ecu.control.trim_enabled) {
        m->trim_limit_since = 0;
        ingest(m, events, cal, 0x27U, MONITOR_UNKNOWN, 0);
        return;
    }
    at_high = (u8)(ecu.control.applied_trim >= high);
    at_low = (u8)(ecu.control.applied_trim <= low);
    if (!at_high && !at_low) {
        m->trim_limit_since = 0;
        ingest(m, events, cal, 0x27U, MONITOR_PASS, 0);
        return;
    }
    if (!m->trim_limit_since)
        m->trim_limit_since = now;
    if (now - m->trim_limit_since < limit_ms)
        return;
    subtype = at_high ? 1U : 2U; /* positive correction=lean; negative=rich */
    ingest(m, events, cal, 0x27U, MONITOR_FAIL, subtype);
}

static void narrowband_events(DiagnosticMonitors *m, OemDtcState *events,
                              const u8 *cal, u32 now) {
    u16 mv = ecu.sensors.oxygen_mv;
    u16 activity_ms = get16(cal + CAL_DTC_O2_ACTIVITY_MS);
    u16 slow_ms = get16(cal + CAL_DTC_O2_SLOW_MS);
    u8 side, active;
    if (cal[0x600]) {
        mode_unavailable(m, events, 0x2DU);
        mode_unavailable(m, events, 0x3FU);
        mode_unavailable(m, events, 0x40U);
        mode_unavailable(m, events, 0x45U);
        return;
    }
    if (!activity_ms)
        activity_ms = 5000U;
    if (!slow_ms)
        slow_ms = 1500U;
    if (ecu.sensors.oxygen.quality != QUALITY_VALID) {
        u8 subtype = ecu.sensors.oxygen.quality == QUALITY_RANGE
                         ? (mv >= 2500U ? 1U : 2U)
                         : 8U;
        ingest(m, events, cal, 0x45U, MONITOR_FAIL, subtype);
        ingest(m, events, cal, 0x2DU, MONITOR_FAIL, 1U);
        return;
    }
    side = mv > (u16)cal[0x8C4] * 5U ? 2U :
           (mv < (u16)cal[0x8C3] * 5U ? 1U : m->o2_side);
    active = (u8)(ecu.control.trim_enabled && ecu.control.mode == ENGINE_RUNNING);
    if (side && side != m->o2_side) {
        if (m->o2_last_cross_at) {
            u32 elapsed = now - m->o2_last_cross_at;
            /* Separate lean-to-rich and rich-to-lean response events. */
            ingest(m, events, cal, side == 2U ? 0x3FU : 0x40U,
                   active && elapsed > slow_ms ? MONITOR_FAIL : MONITOR_PASS, 1U);
        }
        m->o2_last_cross_at = now;
        m->o2_crossings++;
        m->o2_side = side;
    }
    if (!m->o2_window_at)
        m->o2_window_at = now;
    if (active && now - m->o2_window_at >= activity_ms) {
        ingest(m, events, cal, 0x45U,
               m->o2_crossings ? MONITOR_PASS : MONITOR_FAIL, 4U);
        ingest(m, events, cal, 0x2DU,
               m->o2_crossings ? MONITOR_PASS : MONITOR_FAIL, 1U);
        m->o2_crossings = 0;
        m->o2_window_at = now;
    } else if (!active) {
        m->o2_crossings = 0;
        m->o2_window_at = now;
    }
    m->o2_last_mv = mv;
}

static void iac_events(DiagnosticMonitors *m, OemDtcState *events, const u8 *cal) {
    u8 fault = ecu.iac.fault, event = 0x56U, subtype = 1U;
    if (!fault) {
        ingest(m, events, cal, 0x4DU, MONITOR_PASS, 0);
        ingest(m, events, cal, 0x56U, MONITOR_PASS, 0);
        return;
    }
    /* L9935 response faults are electrical; homing/position/deadline faults
       belong to the actuator-position supervisor. */
    if (fault == 2U) subtype = 2U;       /* repeated open-load response */
    else if (fault == 3U) subtype = 4U;  /* driver diagnostic 10b */
    else if (fault == 4U) subtype = 8U;  /* driver diagnostic 00b */
    else {
        event = 0x4DU;
        subtype = fault == 5U ? 4U : 8U;
    }
    ingest(m, events, cal, event, MONITOR_FAIL, subtype);
}

static u8 processor_self_test(void) {
    volatile u16 ram[4];
    volatile u16 a = 0x1357U, b = 0x2468U;
    u8 i;
    static const u16 pattern[4] = {0x0000U, 0xFFFFU, 0x55AAU, 0xAA55U};
    for (i = 0; i < 4U; i++)
        ram[i] = pattern[i];
    for (i = 0; i < 4U; i++)
        if (ram[i] != pattern[i])
            return 0;
    if ((u16)(a + b) != 0x37BFU || (u16)(b - a) != 0x1111U ||
        (u16)(a ^ b) != 0x373FU || (u16)(a * 3U) != 0x3A05U)
        return 0;
    for (i = 0; i < 4U; i++)
        if (((1U << i) & 0x000FU) == 0U)
            return 0;
    return 1;
}

static void self_test_events(DiagnosticMonitors *m, OemDtcState *events,
                             const u8 *cal) {
    if (!m->processor_test)
        m->processor_test = processor_self_test() ? 1U : 2U;
    ingest(m, events, cal, 0x50U,
           m->processor_test == 1U ? MONITOR_PASS : MONITOR_FAIL, 1U);
    /* Event 0x66/P0605 remains unsupported until the post-link image carries
       an excluded-range checksum manifest. A checksum of selected constants
       would not be a program-memory integrity test. */
}

void diagnostic_monitors_update(DiagnosticMonitors *m, OemDtcState *events,
                                u32 now, const u8 *cal) {
    u32 elapsed;
    u8 tps_performance = 0, map_performance = 0;
    u8 iat_performance = 0, clt_performance = 0, battery_performance = 0;
    if (!m->initialized)
        diagnostic_monitors_init(m);
    memset(m->completed, 0, sizeof(m->completed));
    elapsed = now - m->last_update;
    if (m->sensor_seeded) {
        tps_performance = excessive_slew(ecu.sensors.tps.value, m->previous_tps,
            get16(cal + CAL_DTC_TPS_SLEW) ? get16(cal + CAL_DTC_TPS_SLEW) : 30000U,
            elapsed);
        map_performance = excessive_slew(ecu.sensors.map.value, m->previous_map,
            get16(cal + CAL_DTC_MAP_SLEW) ? get16(cal + CAL_DTC_MAP_SLEW) : 3000U,
            elapsed);
        iat_performance = excessive_slew(ecu.sensors.iat.value, m->previous_iat,
            get16(cal + CAL_DTC_TEMP_SLEW) ? get16(cal + CAL_DTC_TEMP_SLEW) : 100U,
            elapsed);
        clt_performance = excessive_slew(ecu.sensors.clt.value, m->previous_clt,
            get16(cal + CAL_DTC_TEMP_SLEW) ? get16(cal + CAL_DTC_TEMP_SLEW) : 100U,
            elapsed);
        if (ecu.control.mode == ENGINE_RUNNING)
            battery_performance = excessive_slew(ecu.sensors.battery.value,
                m->previous_battery,
                get16(cal + CAL_DTC_BATTERY_SLEW) ?
                    get16(cal + CAL_DTC_BATTERY_SLEW) : 50000U,
                elapsed);
    }
    m->last_update = now;
    sensor_event(m, events, cal, 0x1BU, 8U, ecu.sensors.tps.quality, tps_performance);
    sensor_event(m, events, cal, 0x43U, 0U, ecu.sensors.map.quality, map_performance);
    sensor_event(m, events, cal, 0x5BU, 11U, ecu.sensors.iat.quality, iat_performance);
    sensor_event(m, events, cal, 0x61U, 10U, ecu.sensors.clt.quality, clt_performance);
    voltage_event(m, events, cal, battery_performance);
    vss_event(m, events, cal);
    crank_events(m, events, cal);
    phase_events(m, events, cal, now);
    misfire_events(m, events, cal);
    trim_event(m, events, cal, now);
    narrowband_events(m, events, cal, now);
    iac_events(m, events, cal);
    self_test_events(m, events, cal);
    m->previous_tps = ecu.sensors.tps.value;
    m->previous_map = ecu.sensors.map.value;
    m->previous_iat = ecu.sensors.iat.value;
    m->previous_clt = ecu.sensors.clt.value;
    m->previous_battery = ecu.sensors.battery.value;
    m->sensor_seeded = 1;
}
