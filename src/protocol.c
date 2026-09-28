#include "ecu.h"
#include "lifecycle.h"
#include "oem_runtime.h"
#include "faults.h"
#include "identity.h"
#include <string.h>
void protocol_receive(u8 byte) SHARED {
    Protocol *p = &ecu.protocol;
    u8 next = (u8)((p->head + 1U) & 127U);
    if (next == p->tail) {
        p->overflow = 1;
        return;
    }
    p->ring[p->head] = byte;
    p->head = next;
}
static void reply(const u8 *bytes, u8 length) {
    Protocol *p = &ecu.protocol;
    u16 i;
    u8 checksum = length;
    p->tx[0] = 0x55;
    p->tx[1] = length;
    for (i = 0; i < length; i++) {
        p->tx[i + 2] = bytes[i];
        checksum = (u8)(checksum + bytes[i]);
    }
    p->tx[length + 2U] = checksum;
    p->tx_position = 0;
    p->tx_length = (u16)(length + 3U);
}
static void status(u8 ok) {
    u8 value = ok ? 0 : 1;
    reply(&value, 1);
}
static void monitor(void) {
    u8 b[98];
    Controls *s = &ecu.control;
    Sensors *in = &ecu.sensors;
    Rotation r;
    EnginePlan plan;
    u16 lock, i;
    u32 now;
    memset(b, 0, sizeof(b));
    lock = hal_lock();
    r = ecu.rotation;
    plan = ecu.authority.plan;
    now = ecu.milliseconds;
    hal_unlock(lock);
    put16(b, r.rpm);
    put16(b + 2, (u16)in->map.value);
    b[4] = (u8)(in->tps.value / 10);
    b[5] = (u8)(s->ve > 255U ? 255U : s->ve);
    put16(b + 6, plan.pulse_us);
    put16(b + 8, (u16)(plan.advance10 / 10));
    put16(b + 10, plan.dwell_us);
    /* Legacy signed bytes saturate; revision-1 extension retains full values. */
    b[12] = (u8)(s8)clamp32(in->clt.value, -128, 127);
    b[13] = (u8)(s8)clamp32(in->iat.value, -128, 127);
    b[14] = (u8)(in->battery.value / 100);
    b[15] = (u8)in->afr10;
    b[16] = s->target_afr;
    b[17] = s->warmup;
    b[18] = s->afterstart;
    b[19] = (u8)(s->applied_trim / 8);
    b[20] = (u8)((s32)s->applied_trim * 100L / 1024L - 100L);
    b[22] = s->rich;
    put16(b + 25, (u16)s->tps_rate);
    b[29] = (u8)(s->ae_percent > 100U);
    b[30] = (u8)(s->ae_percent > 255U ? 255U : s->ae_percent);
    b[32] = (u8)(s->idle_spark10 / 10);
    b[33] = (u8)ecu.iac.target;
    b[34] = (u8)ecu.iac.position;
    put16(b + 35, s->idle_target);
    b[37] = cal_active()[0x5D8];
    b[38] = ecu.iac.state;
    b[40] = s->boost;
    b[41] = (u8)in->speed_kph;
    b[42] = vehicle.gear;
    b[43] = (u8)((ecu.iac.state == IAC_HOMING ? 1 : 0) | (ecu.iac.state == IAC_READY ? 2 : 0) |
                 (s->key_on ? 4 : 0) | (ecu.iac.fault ? 8 : 0));
    b[44] = ecu.iac.fault;
    b[45] = r.tooth;
    b[46] = (u8)(r.losses > 255 ? 255 : r.losses);
    b[47] = ecu.iac.response;
    b[48] = 0;
    b[49] = (u8)((r.state == ROT_VALID ? 1 : 0) | (s->mode == ENGINE_CRANKING ? 2 : 0) |
                 (s->afterstart > 100U ? 4 : 0) | (s->warmup > 100 ? 8 : 0) | (s->dfco ? 16 : 0) |
                 (s->trim_enabled ? 32 : 0) | (s->fan ? 64 : 0) | (s->pump ? 128 : 0));
    b[50] =
        (u8)((s->rev_limited ? 1 : 0) | (s->launch ? 4 : 0) | (vehicle.antilag ? 8 : 0) | (plan.spark_cut ? 16 : 0) |
             (plan.fuel_cut ? 32 : 0) | (ecu.cal.dirty ? 64 : 0) | (ecu.storage.phase ? 128 : 0));
    b[51] = cal_active()[0x5D4];
    b[52] = ecu.storage.phase;
    b[53] = s->gauge;
    {
        static const u8 channels[6] = {8, 0, 10, 11, 6, 5};
        lock = hal_lock();
        for (i = 0; i < 6; i++)
            put16(b + 54 + 2 * i, ecu.adc[channels[i]].raw);
        hal_unlock(lock);
    }
    put16(b + 66, (u16)((now - s->running_at) / 1000UL));
    b[75] = s->dfco;
    put16(b + 76, s->ve);
    put16(b + 78, s->ae_percent);
    b[80] = TU744_MONITOR_EXTENSION;
    b[81] = (u8)((in->clt.value > 127 || in->clt.value < -128 ? 1U : 0U) |
                 (in->iat.value > 127 || in->iat.value < -128 ? 2U : 0U));
    put16(b + 82, (u16)in->clt.value);
    put16(b + 84, (u16)in->iat.value);
    put16(b + 86, (u16)plan.advance10);
    b[88] = ecu.diagnostics.mil_output;
    /* Extension 2: inhibits and native DTC counts, so a logger needs only
       this command. Active means the stored record is currently failing. */
    lock = hal_lock();
    put16(b + 89, ecu.authority.inhibits);
    hal_unlock(lock);
    if (oem_runtime.booted && oem_runtime.state.events.store.count <= OEM_DTC_SLOTS) {
        const OemDtcRecords *store = &oem_runtime.state.events.store;
        b[91] = store->count;
        for (i = 0; i < store->count; i++)
            b[92] += (u8)(store->records[i][2] & 1U);
    }
    /* Display state, independent of the closed-loop controller's hysteresis.
       Bosch training section XVIII (PDF p.39) supplies voltage polarity;
       the tune supplies thresholds. The middle band is a display convention. */
    if (ecu.cal.valid && !cal_active()[0x600] && in->oxygen.quality == QUALITY_VALID &&
        now - in->stamp <= get16(cal_active() + CAL_SENSOR_AGE)) {
        if (in->oxygen_mv < (u16)cal_active()[0x8C3] * 5U)
            b[93] = 1; /* lean */
        else if (in->oxygen_mv > (u16)cal_active()[0x8C4] * 5U)
            b[93] = 4; /* rich */
        else
            b[93] = 2; /* stoich / calibrated transition band */
    }
    reply(b, 98);
}
/* Command 13: compact live frame, version 2 (40 bytes, big endian). The
   fields a tuner watches while driving, and the map cell the fuel plan
   used, for cell-accurate autotune. Command 10 stays for older loggers. */
static void live_frame(void) {
    u8 b[40];
    Controls *s = &ecu.control;
    Sensors *in = &ecu.sensors;
    const u8 *c = cal_active();
    Rotation r;
    EnginePlan plan;
    u16 lock, inhibits, generation;
    u32 now;
    s32 v;
    lock = hal_lock();
    r = ecu.rotation;
    plan = ecu.authority.plan;
    now = ecu.milliseconds;
    inhibits = ecu.authority.inhibits;
    generation = ecu.cal.generation;
    hal_unlock(lock);
    memset(b, 0, sizeof(b));
    b[0] = 2;
    put16(b + 1, r.rpm);
    put16(b + 3, (u16)clamp32(in->map.value, 0, 65535L));
    put16(b + 5, (u16)clamp32(in->tps.value, 0, 1000));
    b[7] = (u8)clamp32((s32)in->clt.value + 40, 0, 255);
    b[8] = (u8)clamp32((s32)in->iat.value + 40, 0, 255);
    b[9] = (u8)clamp32(in->battery.value / 100, 0, 255);
    if (ecu.cal.valid && c[0x600] && in->oxygen.quality == QUALITY_VALID)
        b[10] = (u8)(in->afr10 > 255U ? 255U : in->afr10);
    v = (s32)(in->oxygen_mv / 5U);
    b[11] = (u8)(v > 255 ? 255 : v);
    b[12] = s->target_afr;
    put16(b + 13, s->applied_trim);
    put16(b + 15, s->ve);
    put16(b + 17, plan.pulse_us);
    put16(b + 19, (u16)plan.advance10);
    b[21] = s->warmup;
    b[22] = s->afterstart;
    b[23] = (u8)(s->ae_percent > 255U ? 255U : s->ae_percent);
    b[24] = (u8)(ecu.iac.position > 255U ? 255U : ecu.iac.position);
    b[25] = (u8)(s->idle_target / 10U > 255U ? 255U : s->idle_target / 10U);
    b[26] = (u8)(in->speed_kph > 255U ? 255U : in->speed_kph);
    b[27] = s->cell_rpm;
    b[28] = (u8)(s->cell_rpm_fraction > 255U ? 255U : s->cell_rpm_fraction);
    b[29] = s->cell_load;
    b[30] = (u8)(s->cell_load_fraction > 255U ? 255U : s->cell_load_fraction);
    b[31] = (u8)((r.state == ROT_VALID ? 1 : 0) | (s->mode == ENGINE_CRANKING ? 2 : 0) |
                 (s->mode == ENGINE_RUNNING ? 4 : 0) | (s->dfco ? 8 : 0) |
                 (s->trim_enabled ? 16 : 0) | (s->fan ? 32 : 0) | (s->pump ? 64 : 0) |
                 (s->launch ? 128 : 0));
    b[32] = (u8)((s->rev_limited ? 1 : 0) | (plan.fuel_cut ? 2 : 0) | (plan.spark_cut ? 4 : 0) |
                 (vehicle.antilag ? 8 : 0) | (ecu.cal.dirty ? 16 : 0) |
                 (ecu.storage.phase ? 32 : 0) | (ecu.diagnostics.mil_output ? 64 : 0) |
                 (ecu.cal.staging ? 128 : 0));
    /* Low nibble: narrowband band as command 10 byte 93; high nibble: gear. */
    if (ecu.cal.valid && !c[0x600] && in->oxygen.quality == QUALITY_VALID &&
        now - in->stamp <= get16(c + CAL_SENSOR_AGE))
        b[33] = (u8)(in->oxygen_mv < (u16)c[0x8C3] * 5U ? 1U :
                     in->oxygen_mv > (u16)c[0x8C4] * 5U ? 4U : 2U);
    b[33] |= (u8)((vehicle.gear > 15U ? 15U : vehicle.gear) << 4);
    put16(b + 34, inhibits);
    put16(b + 36, generation);
    put16(b + 38, (u16)(r.losses > 65535UL ? 65535U : r.losses));
    reply(b, 40);
}
static void execute(void) {
    Protocol *p = &ecu.protocol;
    u8 *b = p->packet;
    u8 n = p->length, cmd = b[0], ok = 0, count;
    u16 at, lock, i;
    u8 out[128];
    u32 now;
    lock = hal_lock();
    now = ecu.milliseconds;
    hal_unlock(lock);
    if (cmd == 0 && n == 1) {
        static const u8 version[] = TU744_ECU_NAME " " TU744_FIRMWARE_VERSION;
        reply(version, (u8)(sizeof(version) - 1));
        return;
    }
    if (cmd == 0x10 && n == 1) {
        monitor();
        return;
    }
    if (cmd == 0x13 && n == 1) {
        live_frame();
        return;
    }
    if (cmd == 0x01 && n == 1) {
        /* Firmware update, the LRE-B4 path: status 00, then the RAM handler
           owns the line (FWUPDATE.A66). Engine stopped: service_enter
           latches it and inhibits outputs; no calibration job may be open. */
        if (!ecu.storage.phase && !ecu.cal.staging) {
            service_enter(now);
            ok = (u8)(ecu.service && ecu.control.mode == ENGINE_STOPPED && !ecu.rotation.rpm);
        }
        if (ok) {
            p->update = 1;
            p->update_at = now;
        } else
            p->rejected++;
        status(ok);
        return;
    }
    if (cmd == 0x11 && n == 1) {
        lock = hal_lock();
        for (i = 0; i < 16; i++)
            put16(out + 2 * i, ecu.adc[i].raw);
        hal_unlock(lock);
        reply(out, 32);
        return;
    }
    if (cmd == 0x31 && n == 1) { /* TPS calibration snapshot, version 1. */
        u32 age;
        lock = hal_lock();
        age = ecu.milliseconds - ecu.adc[8].stamp;
        out[0] = 1;
        out[1] = (u8)((ecu.adc[8].seen && age <= 100UL && ecu.adc[8].raw <= 1023U ? 1U : 0U) |
            (ecu.control.mode == ENGINE_STOPPED && ecu.rotation.state == ROT_UNSYNCED &&
             !ecu.rotation.rpm ? 2U : 0U) | (ecu.key_input ? 4U : 0U) |
            (ecu.cal.valid ? 8U : 0U) | (ecu.iac.state == IAC_HOMING ? 16U : 0U));
        put16(out + 2, ecu.adc[8].raw);
        put16(out + 4, (u16)(age > 65535UL ? 65535UL : age));
        put16(out + 6, get16(cal_active() + 0x7B0));
        put16(out + 8, get16(cal_active() + 0x7B2));
        put16(out + 10, ecu.cal.generation);
        hal_unlock(lock);
        reply(out, 12);
        return;
    }
    if (cmd == 0x20 && n == 1) { /* Capabilities, versioned extension, exact-DTC parity false */
        out[0] = 3;
        out[1] = CAL_SCHEMA;
        put16(out + 2, CAL_SIZE);
        put16(out + 4, ecu.cal.generation);
        out[6] = 32;
        out[7] = 128;
        out[8] = 0;
        out[9] = ecu.board_released;
        reply(out, 10);
        return;
    }
    if (cmd == 0x2D && n == 1) {
        out[0] = 1;
        out[1] = STOCK_95080 ? 1 : 0;
        put16(out + 2, EEPROM_BYTES);
        out[4] = 2; /* Durable calibration copies. */
        out[5] = HISTORY_SLOTS;
        out[6] = out[7] = 0;
        reply(out, 8);
        return;
    }
    if (cmd == 0x2E && n == 1) {
        lock = hal_lock();
        out[0] = 1;
        out[1] = ecu.authority.plan.dwell_feedback;
        out[2] = ecu.authority.spark_draining;
        out[3] = 0;
        for (i = 0; i < 2U; i++) {
            put16(out + 4U + i * 2U, ecu.authority.feedback_missing[i]);
            put16(out + 8U + i * 2U, ecu.authority.feedback_invalid[i]);
            put16(out + 12U + i * 2U, (u16)ecu.authority.feedback_correction[i]);
        }
        put16(out + 16, ecu.authority.late_events);
        hal_unlock(lock);
        reply(out, 18);
        return;
    }
    if (cmd == 0x2F && (n == 1 || (n == 2 && (b[1] == 1U || b[1] == 2U)))) {
        /* Timing health, version 1: worst foreground pass, interval between
           passes, tick service gap and plan age (ms), capture overruns and
           late output stages. Request byte 1 = 1 clears the maxima after
           this report. */
        lock = hal_lock();
        out[0] = 1;
        out[1] = 0;
        put16(out + 2, timing_health.pass_max_ms);
        put16(out + 4, timing_health.interval_max_ms);
        put16(out + 6, timing_health.tick_max_ms);
        put16(out + 8, timing_health.plan_age_max_ms);
        put16(out + 10, timing_health.capture_overruns);
        put16(out + 12, ecu.authority.late_events);
        if (n == 2 && b[1] == 1U)
            timing_health.pass_max_ms = timing_health.interval_max_ms =
                timing_health.tick_max_ms = timing_health.plan_age_max_ms = 0;
        if (n == 2 && b[1] == 2U) {
            /* Version 2 appends the first capture failure (bytes 14..33),
               recovered resynchronizations (34..35) and the failed block's
               30 capture words (36..95). Level 15 publishes the reason last
               and never changes the snapshot again; zero means none yet. */
            out[0] = 2;
            memset(out + 14, 0, 82);
            put16(out + 14, timing_health.capture_reason);
            if (get16(out + 14)) {
                put16(out + 16, timing_health.capture_counter);
                put16(out + 18, timing_health.capture_expected);
                put16(out + 20, timing_health.capture_last);
                put16(out + 22, timing_health.capture_latest);
                put16(out + 24, timing_health.capture_irq);
                put16(out + 26, timing_health.capture_pec);
                put16(out + 28, timing_health.capture_dest);
                out[30] = timing_health.capture_head;
                out[31] = timing_health.capture_tail;
                out[32] = timing_health.capture_next;
                out[33] = timing_health.capture_count;
                for (i = 0; i < 30U; i++)
                    put16(out + 36U + i * 2U, timing_health.capture_words[i]);
            }
            put16(out + 34, timing_health.capture_resyncs);
        }
        hal_unlock(lock);
        reply(out, (u8)(n == 2 && b[1] == 2U ? 96U : 14U));
        return;
    }
    if (cmd == 0x2B && n == 1) {
        out[0] = 1;
        out[1] = FAULT_COUNT;
        put16(out + 2, faults.active);
        put16(out + 4, faults.stored);
        out[6] = faults.loaded;
        out[7] = faults.dirty;
        out[8] = faults.phase;
        out[9] = faults.result;
        out[10] = 0;
        out[11] = 0;
        reply(out, 12);
        return;
    }
    if (cmd == 0x2C && n == 2 && b[1] >= 1U && b[1] <= FAULT_COUNT) {
        const FaultRecord *r = &faults.record[b[1] - 1U];
        u16 mask = (u16)(1U << (b[1] - 1U));
        out[0] = 1;
        out[1] = b[1];
        out[2] = (u8)((faults.active & mask) != 0);
        out[3] = (u8)((faults.stored & mask) != 0);
        out[4] = r->reason;
        out[5] = 0;
        put16(out + 6, r->occurrences);
        put32(out + 8, r->first);
        put32(out + 12, r->last);
        reply(out, 16);
        return;
    }
    if ((cmd == 0x21 || cmd == 0x22) && n == 1) {
        p->job = cmd; /* stepped: see job_step */
        p->step = 0;
        return;
    } else if (cmd == 0x23 && n == 1) {
        cal_abort();
        ok = 1;
    } else if (cmd == 0x24 && n == 1)
        ok = storage_save(now);
    else if (cmd == 0x25 && n == 1) {
        lock = hal_lock();
        put16(out, ecu.authority.inhibits);
        put16(out + 2, ecu.authority.epoch);
        put16(out + 4, ecu.cal.generation);
        put16(out + 6, ecu.cal.error_offset);
        out[8] = ecu.storage.result;
        out[9] = ecu.service;
        out[10] = ecu.iac.state;
        out[11] = ecu.iac.fault;
        put16(out + 12, ecu.iac.position);
        put16(out + 14, ecu.iac.target);
        put16(out + 16, ecu.authority.plan.requested_us);
        put16(out + 18, ecu.authority.plan.pulse_us);
        /* Retired quality telemetry; reserve its offsets for framing stability. */
        memset(out + 20, 0, 6);
        out[26] = ecu.control.trim_enabled;
        out[27] = ecu.control.idle_mode;
        put32(out + 28, ecu.authority.faults);
        out[32] = ecu.diagnostics.mil_steady;
        out[33] = ecu.diagnostics.mil_flashing_requested;
        hal_unlock(lock);
        reply(out, 34);
        return;
    } else if (cmd == 0x27 && n == 1) {
        out[0] = 1; /* Lifecycle status version, not a stock diagnostic mode. */
        out[1] = power.state;
        out[2] = power.release_blocked;
        out[3] = wideband.state;
        out[4] = vehicle.armed;
        out[5] = vehicle.stationary;
        out[6] = vehicle.gear;
        out[7] = oem_runtime.booted;
        out[8] = oem_runtime.initialized;
        out[9] = oem_runtime.fault;
        put16(out + 10, oem_runtime.bindings);
        put16(out + 12, oem_runtime.adc_fresh);
        out[14] = oem_runtime.history.ready;
        out[15] = oem_runtime.history.dirty;
        out[16] = oem_runtime.history.journal.phase;
        out[17] = oem_runtime.history.journal.result;
        out[18] = oem_runtime.history.clear_pending;
        out[19] = oem_runtime.history.clear_durable;
        out[20] = oem_runtime.state.events.store.count;
        out[21] = ecu.diagnostics.mil_steady;
        out[22] = ecu.diagnostics.mil_flashing_requested;
        out[23] = 0; /* Full native lifecycle parity is NOT asserted. */
        reply(out, 24);
        return;
    } else if (cmd == 0x2A && n == 1) {
        out[0] = 1; /* Raw boot observation, separate versioned response. */
        out[1] = (u8)(reset_capture_marker == RESET_CAPTURE_VALID);
        put16(out + 2, out[1] ? reset_capture_raw : 0U);
        put16(out + 4, RESET_KNOWN_MASK);
        out[6] = (u8)(out[1] && (reset_capture_raw & RESET_KNOWN_MASK));
        out[7] = 0; /* Reserved; no inferred power-on/software/trap cause. */
        reply(out, 8);
        return;
    } else if (cmd == 0x28 && n == 2 && b[1] <= 2U) {
        if (!b[1]) {
            launch_disarm();
            ok = 1;
        } else
            ok = launch_arm((u8)(b[1] == 2U), now);
    } else if (cmd == 0x30 && n == 1) {
        /* Clear all stored DTCs: native records (P-codes, freeze frames, MIL
           demand) and inactive private LRE records. Engine stopped, key on,
           diagnostics running. The runtime services the admitted request on
           its next release; history is saved at key-off. */
        OemRuntime *r = &oem_runtime;
        if (ecu.key_input && ecu.control.mode == ENGINE_STOPPED && !ecu.rotation.rpm &&
            r->booted && r->initialized && !r->stopped && !r->fault &&
            (r->state.events.gate & 32U) &&
            oem_history_owner_clear_request(&r->history, &r->state, OEM_CLEAR_ALL, 0)) {
            faults_clear();
            ok = 1;
        }
    } else if (cmd == 0x29 && n == 2 && b[1] < OEM_DTC_SLOTS && oem_runtime.booted) {
        reply(oem_runtime.state.events.store.records[b[1]], OEM_DTC_RECORD_SIZE);
        return;
    } else if (cmd == 0x26 && n == 2 && b[1] < 128) {
        out[0] = b[1];
        out[1] = ecu.diagnostics.support[b[1]];
        put16(out + 2, ecu.diagnostics.live[b[1]]);
        reply(out, 4);
        return;
    } else if (cmd == 0x32 && n >= 5 && n == (u8)(b[3] + 4U)) {
        /* Live map-cell write: offset:u16, count:u8 (1..16), bytes. Reply is
           status, generation:u16, live-edit counter:u16. */
        ok = cal_live_write(get16(b + 1), b + 4, b[3]);
        if (!ok)
            p->rejected++;
        lock = hal_lock();
        out[0] = ok ? 0 : 1;
        put16(out + 1, ecu.cal.generation);
        put16(out + 3, ecu.cal.live_edits);
        hal_unlock(lock);
        reply(out, 5);
        return;
    } else if (cmd == 0x04 && n == 4) {
        at = get16(b + 1);
        count = b[3];
        if (count && count <= 128 && at <= CAL_SIZE && count <= CAL_SIZE - at) {
            reply(cal_active() + at, count);
            return;
        }
    } else if (cmd == 0x05 && n >= 4) {
        at = get16(b + 1);
        count = b[3];
        if (count && count <= 32 && n == (u8)(count + 4U) && at <= CAL_SIZE &&
            count <= CAL_SIZE - at) {
            /* Legacy single writes are complete validated transactions (a
               stepped job). Explicit begin/write/commit supports atomic
               multi-page axes/maps; a write inside it is only a copy. */
            if (!ecu.cal.staging) {
                p->job = cmd;
                p->step = 0;
                return;
            }
            ok = cal_write(at, b + 4, count);
        }
    }
    /* Legacy direct EEPROM writes and opaque flash-handler jump are rejected.
       They bypass schema/last-good storage and cannot be made safe by length checks. */
    if (!ok)
        p->rejected++;
    status(ok);
}
static void job_finish(u8 ok) {
    Protocol *p = &ecu.protocol;
    p->job = 0;
    if (!ok)
        p->rejected++;
    status(ok);
}
/* One step of a calibration transaction. The copy, validation and
   structural comparison each take several ms of the 30 ms plan age; run in
   one pass they held the foreground for ~26 ms and a running engine's plan
   went stale. Staging is not written by anyone else between steps; key-off
   abort is detected by the next step. */
static void job_step(void) {
    Protocol *p = &ecu.protocol;
    u8 *b = p->packet, stopped = (u8)(ecu.control.mode == ENGINE_STOPPED);
    switch (p->job) {
    case 0x21:
        job_finish(cal_begin());
        return;
    case 0x22:
        if (p->step++ == 0) {
            if (!cal_commit_check()) job_finish(0);
            return;
        }
        job_finish(cal_commit_apply(stopped));
        return;
    default: /* 0x05 as an implicit transaction */
        if (p->step == 0) {
            p->step = 1;
            if (!cal_begin()) job_finish(0);
            return;
        }
        if (p->step == 1) {
            p->step = 2;
            if (cal_write(get16(b + 1), b + 4, b[3]) && cal_commit_check()) return;
        } else if (cal_commit_apply(stopped)) {
            job_finish(1);
            return;
        }
        cal_abort();
        job_finish(0);
        return;
    }
}
/* Called by the UART receive interrupt when the echo of the previous reply
   byte returns (single-wire K-line): send the next byte at once instead of
   waiting for the next foreground pass (~10-20 ms per byte). */
void protocol_tx_continue(void) SHARED {
    Protocol *p = &ecu.protocol;
    if (p->tx_length && p->tx_position < p->tx_length) {
        hal_uart_send(p->tx[p->tx_position++]);
        if (p->tx_position == p->tx_length)
            p->tx_length = 0;
    }
}
/* Complete any calibration job at once (host tests and tools). */
void protocol_poll(u32 now) {
    protocol_service(now, 255);
}
/* The foreground passes steps=1 only in a pass that has just published a
   plan, else 0: a calibration job then never holds a plan past its age. New
   frames wait in the receive ring until the job has replied. */
void protocol_service(u32 now, u8 steps) {
    Protocol *p = &ecu.protocol;
    u8 byte, budget = 32;
    u16 lock;
    cal_poll(now);
    if (p->job) {
        while (steps-- && p->job)
            job_step();
        return;
    }
    if (p->update && !p->tx_length) {
        /* The accepted status has gone out; hal_uart_ready also waits for its
           echo. Give an in-flight IAC off command up to 200 ms. */
        lock = hal_lock();
        byte = hal_uart_ready();
        hal_unlock(lock);
        if (byte && (!ecu.iac.off_pending || now - p->update_at > 200UL)) {
            hal_firmware_update();
            p->update = 0; /* Only reached when the handler cannot start. */
        }
        return;
    }
    if (p->tx_length) {
        /* Starts a reply, and resumes it when an echo is missing (timeout). */
        lock = hal_lock();
        if (p->tx_length && hal_uart_ready()) {
            hal_uart_send(p->tx[p->tx_position++]);
            if (p->tx_position == p->tx_length)
                p->tx_length = 0;
        }
        hal_unlock(lock);
        return;
    }
    lock = hal_lock();
    if (p->overflow) {
        p->tail = p->head;
        p->overflow = 0;
        p->state = 0;
        p->dropped++;
    }
    hal_unlock(lock);
    if (p->state && now - p->started > 100UL) {
        p->state = 0;
        p->rejected++;
    }
    while (budget-- && p->tail != p->head) {
        byte = p->ring[p->tail];
        p->tail = (u8)((p->tail + 1U) & 127U);
        if (p->state == 0) {
            if (byte == 0xAA) {
                p->state = 1;
                p->started = now;
            }
            continue;
        }
        if (p->state == 1) {
            if (!byte || byte > 36) {
                p->state = 0;
                p->rejected++;
                continue;
            }
            p->length = byte;
            p->checksum = byte;
            p->used = 0;
            p->state = 2;
            continue;
        }
        if (p->state == 2) {
            p->packet[p->used++] = byte;
            p->checksum = (u8)(p->checksum + byte);
            if (p->used == p->length)
                p->state = 3;
            continue;
        }
        p->state = 0;
        if (byte == p->checksum) {
            execute();
            while (steps-- && p->job)
                job_step();
            return;
        }
        p->rejected++;
    }
}
