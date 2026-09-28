#include "oem.h"
#include <string.h>

static u16 word(const u8 *r, u8 at) {
    return (u16)((u16)r[at] | ((u16)r[at + 1U] << 8));
}
static void put_word(u8 *r, u8 at, u16 value) {
    r[at] = (u8)value;
    r[at + 1U] = (u8)(value >> 8);
}
static u8 valid(const OemDtcRecords *s) {
    u8 i;
    if (s->count > OEM_DTC_SLOTS)
        return 0;
    for (i = 0; i < s->count; i++)
        if (s->records[i][5] >= 38U)
            return 0;
    return 1;
}

/* Exact valid-domain behavior of 0x6A13C/0x6A068. In the last-slot branch
   the ROM clears the row WITHOUT decrementing AA64. Do not normalize this
   into a conventional compact-vector erase. Interior erase shifts all bytes. */
u8 oem_dtc_remove(OemDtcRecords *s, u8 slot) {
    u8 i;
    if (!s->count || s->count > OEM_DTC_SLOTS || slot >= s->count)
        return 0;
    if (slot == s->count - 1U) {
        memset(s->records[slot], 0, OEM_DTC_RECORD_SIZE);
        return 1;
    }
    for (i = (u8)(slot + 1U); i < s->count; i++)
        memcpy(s->records[i - 1U], s->records[i], OEM_DTC_RECORD_SIZE);
    s->count--;
    memset(s->records[s->count], 0, OEM_DTC_RECORD_SIZE);
    return 1;
}

/* 0x6AB72..0x6AD2E: confirmation/healing on the selected native phase.
   This callback compares address identity, not the pointed-to byte's value.
   0x69FFA is an unconditional return-1 hook in the reference image.
   Optional eligibility mask is a standalone adapter; the original public
   entry below supplies NULL and retains the complete native behavior. */
u8 oem_dtc_phase_masked(OemDtcRecords *s, u16 phase_address, const u8 *completed) {
    u8 i, *r;
    u16 status, descriptor;
    const u8 *c;
    if (!valid(s))
        return 0;
    for (i = 0; i < s->count; i++) {
        r = s->records[i];
        if (completed && (r[0] >= 107U || !(completed[r[0] / 8U] & (1U << (r[0] & 7U)))))
            continue;
        status = word(r, 4);
        descriptor = word(r, 2);
        c = oem_dtc_configs[r[5]];
        if (descriptor & 1U) {
            if ((status & 1U) || oem_dtc_phase_addresses[c[1]] != phase_address ||
                !(descriptor & 2U))
                continue;
            if (r[6])
                r[6]--;
            if (r[6])
                continue;
            status |= 3U;
            if (c[0])
                status |= 8U;
            if (c[0] == 2U)
                status |= 16U;
            put_word(r, 4, status);
            r[8] = c[10];
        } else {
            if (!(status & 1U) || oem_dtc_phase_addresses[c[3]] != phase_address ||
                !(descriptor & 2U))
                continue;
            if (r[7])
                r[7]--;
            if (r[7])
                continue;
            put_word(r, 4, (u16)(status & 0xFFE6U));
            if (!r[8]) {
                r[0] = 0;
                oem_dtc_remove(s, i);
                /* Native loop increments even after shifting the next row. */
            }
        }
    }
    return 1;
}

u8 oem_dtc_phase(OemDtcRecords *s, u16 phase_address) {
    return oem_dtc_phase_masked(s, phase_address, 0);
}

/* Complete 0x6AA60 including its final 0x6AB72 call. Demand is reduced BEFORE
   the confirmation callback; a new confirmation affects the following pass.
   Demand 2 is retained as data only. This function does not flash the MIL. */
u8 oem_dtc_aggregate(OemDtcRecords *s) {
    u8 i, *r, steady = 0, flashing = 0, active = 0;
    u16 descriptor, status;
    if (!valid(s))
        return 0;
    for (i = 0; i < s->count; i++) {
        r = s->records[i];
        descriptor = word(r, 2);
        if ((descriptor & 3U) == 3U && r[1] < 255U)
            r[1]++;
        status = word(r, 4);
        if (status & 8U)
            steady = 1;
        if (status & 16U)
            flashing = 1;
        if ((status & 64U) && (descriptor & 1U))
            active = 1;
    }
    s->demand = flashing ? 2U : (steady ? 1U : 0U);
    s->active_demand = active;
    s->phases[1] = 1; /* 951A */
    return oem_dtc_phase(s, 0x951AU);
}

static u8 valid_state(const OemDtcState *s, u8 event) {
    u8 i;
    if (event >= 107U || !valid(&s->store))
        return 0;
    for (i = 0; i < s->store.count; i++)
        if (s->store.records[i][0] >= 107U)
            return 0;
    return 1;
}
static void timestamp(OemDtcState *s, u8 *r) {
    /* 0x6B74A stores B2FA high byte then low byte inside the native record. */
    r[20] = (u8)(s->timestamp >> 8);
    r[21] = (u8)s->timestamp;
}
static void snapshot(OemDtcState *s, u8 *r) {
    static const u8 order[11] = {0, 1, 2, 3, 4, 5, 1, 1, 6, 7, 8};
    u8 i;
    for (i = 0; i < 11; i++)
        r[9U + i] = s->context[order[i]];
    timestamp(s, r);
}
static u16 lamp_flags(u16 status, const u8 *c) {
    status |= 3U;
    if (c[0])
        status |= 8U;
    if (c[0] == 2U)
        status |= 16U;
    return status;
}

/* 0x6A1E6: scan in native order, with the 12/8 class limits checked during
   the scan. This can reject before reaching a later matching event. Preserve
   that order rather than imposing a different global replacement policy. */
u8 oem_dtc_assert(OemDtcState *s, u8 event, u16 descriptor) {
    u8 i, *r, config, class_count = 0, result = 0;
    u16 status;
    const u8 *c;
    if (!valid_state(s, event))
        return 0;
    if (!(s->gate & 32U)) {
        s->last_event = event;
        return 0;
    }
    config = oem_dtc_event_configs[event];
    c = oem_dtc_configs[config];
    if (!config)
        result = 1;
    else
        for (i = 0; i < OEM_DTC_SLOTS; i++) {
            r = s->store.records[i];
            if (i == s->store.count) {
                r[0] = 0;
                s->store.count++;
                snapshot(s, r);
                status = (u16)((word(r, 4) & 255U) | ((u16)config << 8));
                s->live[event] |= 0x1000U;
                r[6] = c[2];
                if (s->store.phases[c[1]] == 1 && (descriptor & 2U) && r[6])
                    r[6]--;
                r[7] = c[4];
                put_word(r, 2, (u16)((descriptor & 0x0FFFU) | ((descriptor & 0x0F00U) << 4)));
                status &= 0xFFDCU; /* bits0,1,5 */
                status &= 0xFFE7U; /* bits3,4 */
                if (!r[6])
                    status = lamp_flags(status, c);
                status = (u16)((status & 0xFFFBU) | (c[5] == 1 ? 4U : 0U));
                status = (u16)((status & 0xFFBFU) | (c[12] == 1 ? 64U : 0U));
                put_word(r, 4, status);
                r[8] = status & 1U ? c[10] : c[7];
                r[22] = 1;
                r[1] = 0;
                r[0] = event;
                result = 1;
                break;
            }
            if (r[0] == event) {
                s->live[event] |= 0x1000U;
                if (!(word(r, 2) & 1U)) {
                    status = word(r, 4);
                    status = (u16)((status & 0xFFFBU) | (c[5] == 1 ? 4U : 0U));
                    timestamp(s, r);
                    if (s->store.phases[c[1]] == 1 && (descriptor & 2U) && r[6])
                        r[6]--;
                    r[7] = c[4];
                    put_word(r, 2, (u16)((word(r, 2) & 0xF000U) | (descriptor & 0x0FFFU)));
                    if (!r[6])
                        status = lamp_flags(status, c);
                    r[8] = status & 1U ? c[10] : c[7];
                    if (r[22] < 255U)
                        r[22]++;
                    put_word(r, 4, (u16)(status | 32U));
                }
                result = 1;
                break;
            }
            if (oem_dtc_configs[oem_dtc_event_configs[r[0]]][5] == 1)
                class_count++;
            if ((c[5] == 1 && class_count >= 12U) ||
                (c[5] != 1 && (s16)((s16)i - class_count) >= 8)) {
                s->overflow = 0x55;
                break;
            }
        }
    s->last_event = event;
    return result;
}

/* 0x6A636. The apparent repeated phase test at 0x6A6F2 is unreachable under
   this single-owner input contract: the same byte is read without intervening
   writes. Preserve the observed path; do not invent an inverse test. */
u8 oem_dtc_recover(OemDtcState *s, u8 event, u16 descriptor) {
    u8 i, *r, result = 0;
    u16 status;
    const u8 *c;
    if (!valid_state(s, event))
        return 0;
    c = oem_dtc_configs[oem_dtc_event_configs[event]];
    if (s->gate & 32U) {
        for (i = 0; i < s->store.count; i++) {
            r = s->store.records[i];
            if (r[0] != event)
                continue;
            if (!(word(r, 2) & 1U))
                break;
            status = word(r, 4);
            r[8] = status & 2U ? c[10] : c[9];
            if (!(status & 2U))
                status &= 0xFFFBU;
            if (s->store.phases[c[1]] == 1)
                r[6] = c[2];
            if (s->store.phases[c[3]] == 1 && (descriptor & 2U) && r[7])
                r[7]--;
            put_word(r, 2, (u16)((word(r, 2) & 0xFF00U) | (descriptor & 255U)));
            status &= 0xFFEFU;
            if (!r[7])
                status &= 0xFFF6U;
            put_word(r, 4, status);
            if (!r[7] && !r[8]) {
                r[0] = 0;
                oem_dtc_remove(&s->store, i);
            }
            break;
        }
        result = 1;
    }
    if (s->last_event == event)
        s->last_event = 0;
    return result;
}

u8 oem_dtc_complete(OemDtcState *s, u8 event) {
    u8 i, *r;
    u16 descriptor, status;
    const u8 *c;
    if (!valid_state(s, event) || !(s->gate & 32U))
        return 0;
    c = oem_dtc_configs[oem_dtc_event_configs[event]];
    for (i = 0; i < s->store.count; i++) {
        r = s->store.records[i];
        if (r[0] != event)
            continue;
        descriptor = (u16)(word(r, 2) | 2U);
        put_word(r, 2, descriptor);
        status = word(r, 4);
        if (descriptor & 1U) {
            if (s->store.phases[c[1]] == 1 && r[6]) {
                r[6]--;
                if (!r[6])
                    status = lamp_flags(status, c);
            }
        } else if (s->store.phases[c[3]] == 1 && r[7]) {
            r[7]--;
            if (!r[7])
                status &= 0xFFE6U;
        }
        put_word(r, 4, status);
        break;
    }
    return 1;
}

u8 oem_dtc_subtype(OemDtcState *s, u8 event, u16 descriptor) {
    u8 i, *r;
    if (!valid_state(s, event) || !(s->gate & 32U))
        return 0;
    for (i = 0; i < s->store.count; i++) {
        r = s->store.records[i];
        if (r[0] != event)
            continue;
        put_word(r, 2, (u16)((word(r, 2) & 0xF0FFU) | (descriptor & 0x0F00U)));
        return 1;
    }
    return 0;
}

u8 oem_dtc_ingest(OemDtcState *s, u8 event, u16 *descriptor) {
    u8 actions;
    if (!valid_state(s, event))
        return 0;
    actions = dtc_ingest_action(s->live[event], *descriptor);
    if (actions & DTC_INGEST_NEW) {
        oem_dtc_assert(s, event, *descriptor);
        if (s->live[event] & 0x1000U)
            *descriptor |= 0x1000U;
    } else if (actions & DTC_INGEST_CLEAR)
        oem_dtc_recover(s, event, *descriptor);
    else {
        if (actions & DTC_INGEST_COMPLETE)
            oem_dtc_complete(s, event);
        if (actions & DTC_INGEST_SUBTYPE)
            oem_dtc_subtype(s, event, *descriptor);
    }
    s->live[event] = *descriptor;
    return 1;
}

/* 0x6B252..0x6B4D6. FD6C.6 makes this a once-per-native-phase operation;
   the caller must establish when that phase is reset. This is not a generic
   millisecond timer or a guessed drive/warm-up cycle. The native admission
   hook 0x6AA5C always returns 1 in this image. Erased rows are not compacted. */
u8 oem_dtc_age(OemDtcState *s) {
    u8 i, *r, event;
    const u8 *c;
    u16 descriptor;
    if (!valid_state(s, 0))
        return 0;
    if (s->gate & 64U)
        return 1;
    for (i = 0; i < s->store.count; i++) {
        r = s->store.records[i];
        event = r[0];
        c = oem_dtc_configs[r[5]];
        descriptor = word(r, 2);
        if (!(descriptor & 1U) && (s->live[event] & 2U)) {
            if (r[1] > oem_dtc_event_counts[event]) {
                if (s->store.phases[c[1]] == 1 && r[6])
                    r[6]--;
            } else if (s->store.phases[c[3]] == 1)
                r[6] = c[2];
        }
        if (!(word(r, 4) & 1U)) {
            if (s->store.phases[c[(descriptor & 1U) ? 6 : 8]] == 1 && r[8])
                r[8]--;
        }
        if (!r[8]) {
            s->live[event] &= 0xEFFEU;
            memset(r, 0, OEM_DTC_RECORD_SIZE);
        }
    }
    s->gate |= 64U;
    return 1;
}

/* Standalone preflight for a composed clear; not an OEM routine port. */
u8 oem_dtc_clear_ready(const OemDtcState *s) {
    if (!valid_state(s, 0) || !(s->gate & 32U) ||
        s->clear_inverse != (u16)~s->clear_request)
        return 0;
    if (s->clear_request == 106U)
        return (u8)(s->clear_mode <= 1U);
    return (u8)(s->clear_request >= 1U && s->clear_request <= 105U);
}

/* 0x6B4D8..0x6B696, the deferred RAM clear worker. Request creation, its
   complementary-word validation, RTOS event 0x4702 and persistent history
   belong to other routines. This worker does NOT validate clear_inverse. */
u8 oem_dtc_clear_worker(OemDtcState *s) {
    u8 i, event;
    if (!valid_state(s, 0) || !(s->gate & 32U))
        return 0;
    if (s->clear_request == 106U) {
        if (!s->clear_mode) {
            s->store.count = 0;
            memset(s->store.records, 0, sizeof(s->store.records));
            for (i = 1; i <= 105U; i++)
                s->live[i] = 0;
            s->overflow = 0;
        } else if (s->clear_mode == 1U) {
            /* Native order is descending, including row zero. It clears
               live bit7 but leaves other live bits and count untouched. */
            i = s->store.count;
            while (i) {
                i--;
                event = s->store.records[i][0];
                if (oem_dtc_configs[oem_dtc_event_configs[event]][5] == 1) {
                    s->live[event] &= 0xFF7FU;
                    memset(s->store.records[i], 0, OEM_DTC_RECORD_SIZE);
                }
            }
        }
    } else if (s->clear_request >= 1U && s->clear_request <= 105U) {
        for (i = 0; i < OEM_DTC_SLOTS; i++) {
            if (s->store.records[i][0] == s->clear_request) {
                memset(s->store.records[i], 0, OEM_DTC_RECORD_SIZE);
                s->live[s->clear_request] = 0;
                break;
            }
        }
    }
    s->clear_request = 0;
    s->clear_inverse = 0xFFFFU;
    return 1;
}

/* 0x6B1AA..0x6B250. FD14.15 selects the short initialization path.
   This entry neither fabricates phase completion nor clears aging bit6. */
u8 oem_dtc_cycle_begin(OemDtcState *s) {
    u8 i, *r;
    if (!valid_state(s, 0))
        return 0;
    s->clear_previous = 0;
    if (s->startup_flags & 0x8000U) {
        s->scan_event = 1;
        s->store.demand = 0;
    } else {
        for (i = 0; i < s->store.count; i++) {
            r = s->store.records[i];
            put_word(r, 2, (u16)(word(r, 2) & 0xFFFDU));
            r[1] = 0;
        }
        for (i = 1; i <= 105U; i++)
            s->live[i] &= 0xFFFDU;
    }
    s->store.phases[1] = 1;
    s->gate |= 32U;
    s->clear_request = 0;
    s->clear_inverse = 0xFFFFU;
    return 1;
}

static u8 clear_available(const OemDtcState *s) {
    return (u8)(s->clear_request == 0 && s->clear_inverse == 0xFFFFU);
}

/* 0x69F8C, 0x69F9A, 0x69FDE: drive-phase byte9519 and native counters.
   run_flags is FD16, startup_flags is FD14; no standalone engine-state
   substitution is made by these native-contract functions. */
u8 oem_dtc_drive_init(OemDtcState *s) {
    s->store.phases[2] = 0;
    if (s->startup_flags & 0x8000U)
        s->drive_count = 0;
    return 1;
}
u8 oem_dtc_drive_update(OemDtcState *s) {
    if (!valid_state(s, 0))
        return 0;
    if (!s->store.phases[2]) {
        if (s->run_flags & 4U) {
            if (s->drive_timer >= oem_dtc_drive_delay) {
                s->store.phases[2] = 1;
                oem_dtc_phase(&s->store, 0x9519U);
                if (s->drive_count < 65535U)
                    s->drive_count++;
            } else
                s->drive_timer++;
        } else
            s->drive_timer = 0;
    }
    return 1;
}
u8 oem_dtc_drive_clear(OemDtcState *s) {
    if (s->clear_request == 106U) {
        s->store.phases[2] = 0;
        s->drive_timer = 0;
        s->drive_count = 0;
    }
    return 1;
}

/* 0x6BE44, 0x6BE52, 0x6BEB2: warm-up phase952B. All temperatures are
   native coolant bytes. Absolute comparison is strict >; rise is >=. */
u8 oem_dtc_warmup_init(OemDtcState *s) {
    s->store.phases[3] = 0;
    if (s->startup_flags & 0x8000U)
        s->warmup_count = 0;
    return 1;
}
u8 oem_dtc_warmup_update(OemDtcState *s) {
    if (!valid_state(s, 0))
        return 0;
    if (s->store.phases[3] || (s->gate & 128U))
        return 1;
    if (s->run_flags & 4U) {
        if (s->warmup_start >= oem_dtc_warmup_limits[2])
            s->gate |= 128U;
        else {
            s->gate &= 0xFF7FU;
            if (s->coolant > oem_dtc_warmup_limits[1] &&
                (s16)((s16)s->coolant - s->warmup_start) >= oem_dtc_warmup_limits[0]) {
                s->store.phases[3] = 1;
                oem_dtc_phase(&s->store, 0x952BU);
                if (s->warmup_count < 255U)
                    s->warmup_count++;
            }
        }
    } else
        s->warmup_start = s->coolant;
    return 1;
}
u8 oem_dtc_warmup_clear(OemDtcState *s) {
    if (s->clear_request == 106U) {
        s->store.phases[3] = 0;
        s->warmup_start = s->coolant;
        s->gate = (u16)((s->gate & 0xFF7FU) | (s->coolant >= oem_dtc_warmup_limits[2] ? 128U : 0U));
        s->warmup_count = 0;
    }
    return 1;
}

/* 0x6BE28: divider test precedes increment, so a timestamp increment needs
   361 calls from divider zero. The scheduler establishes the time unit. */
void oem_dtc_clock(OemDtcState *s) {
    if (s->clock_divider < 360U)
        s->clock_divider++;
    else {
        s->clock_divider = 0;
        s->timestamp++;
    }
}

/* 0x6BA14/0x6BA7C/0x6BB22, state changes preceding RTOS event 0x4702.
   A successful return hands the event to the standalone foreground owner.
   The OEM RTOS notification implementation is not copied into this C core. */
u8 oem_dtc_clear_all(OemDtcState *s) {
    u8 i;
    if (!valid_state(s, 0) || !clear_available(s))
        return 0;
    s->clear_request = 106U;
    s->clear_inverse = 0xFF95U;
    s->clear_mode = 0;
    for (i = 1; i <= 105U; i++)
        s->live[i] |= 128U;
    return 1;
}

u8 oem_dtc_clear_emissions(OemDtcState *s) {
    u8 i, event;
    if (!valid_state(s, 0) || !clear_available(s))
        return 0;
    s->clear_request = 106U;
    s->clear_inverse = 0xFF95U;
    s->clear_mode = 1;
    for (i = 0; i < s->store.count; i++) {
        event = s->store.records[i][0];
        if (oem_dtc_configs[oem_dtc_event_configs[event]][5] == 1)
            s->live[event] |= 128U;
    }
    return 1;
}

u8 oem_dtc_clear_event(OemDtcState *s, u8 event) {
    if (!valid_state(s, event) || !clear_available(s))
        return 0;
    s->clear_request = event;
    s->clear_inverse = (u16) ~(u16)event;
    s->live[event] |= 128U;
    return 1;
}

/* Native scans sometimes inspect the first row AFTER the counted region.
   At count20 its first bytes alias B224, the live descriptor array. Model
   that alias explicitly, without dereferencing beyond records[20]. */
static u8 scan_event_at(const OemDtcState *s, u8 slot) {
    return slot < OEM_DTC_SLOTS ? s->store.records[slot][0] : (u8)s->live[0];
}

/* 0x6AD34..0x6B1A8, valid count/event domain. Corrupt count>20 is rejected
   by the standalone boundary instead of executing the ROM's asynchronous
   clear request followed by an out-of-range pool scan. */
u8 oem_dtc_maintain(OemDtcState *s) {
    u8 i, j, event, *r, old_wait;
    u16 descriptor, status;
    const u8 *c;
    if (!valid_state(s, 0))
        return 0;
    s->scan_event++;
    if (s->scan_event > 105U)
        s->scan_event = 1;
    event = s->scan_event;
    if (s->live[event] & 1U) {
        if (oem_dtc_event_configs[event] && s->overflow != 0x55U) {
            for (i = 0; i <= s->store.count; i++) {
                if (scan_event_at(s, i) == event) {
                    descriptor = i < OEM_DTC_SLOTS ? word(s->store.records[i], 2) : s->live[1];
                    if (!(descriptor & 1U)) {
                        if (i < OEM_DTC_SLOTS)
                            put_word(s->store.records[i], 2, (u16)(descriptor | 1U));
                        else
                            s->live[1] |= 1U;
                        oem_dtc_recover(s, event, s->live[event]);
                        s->live[event] &= 0xFFFEU;
                    }
                    break;
                }
                if (i == s->store.count) {
                    s->live[event] &= 0xFFFEU;
                    break;
                }
            }
        }
    } else
        oem_dtc_recover(s, event, s->live[event]);

    if (s->live[event] & 0x1000U) {
        if (!oem_dtc_event_configs[event])
            s->live[event] &= 0xEFFFU;
        else
            for (i = 0; i <= s->store.count; i++) {
                if (scan_event_at(s, i) == event)
                    break;
                if (i == s->store.count) {
                    s->live[event] &= 0xEFFFU;
                    break;
                }
            }
    } else if (oem_dtc_event_configs[event]) {
        for (i = 0; i < s->store.count; i++)
            if (s->store.records[i][0] == event) {
                s->live[event] |= 0x1000U;
                break;
            }
    }

    if (s->store.count) {
        s->scan_record++;
        if (s->scan_record >= s->store.count)
            s->scan_record = 0;
        i = s->scan_record;
        r = s->store.records[i];
        event = r[0];
        if (event) {
            for (j = (u8)(i + 1U); j < s->store.count; j++)
                if (s->store.records[j][0] == event)
                    memset(s->store.records[j], 0, OEM_DTC_RECORD_SIZE);
            c = oem_dtc_configs[oem_dtc_event_configs[event]];
            descriptor = word(r, 2);
            status = word(r, 4);
            if (r[5] != oem_dtc_event_configs[event] || ((status & 8U) && !c[0]) ||
                ((status & 16U) && c[0] != 2U) || ((status & 4U) && !c[5]) ||
                ((descriptor ^ s->live[event]) & 2U) || r[6] > c[2] || r[7] > c[4])
                oem_dtc_clear_event(s, event);
        } else if (i == s->store.count - 1U)
            s->store.count--;
        else {
            /* 6A068 copies ONE row to its predecessor. This path neither
               shifts the remaining tail nor decrements count. */
            memcpy(r, s->store.records[i + 1U], OEM_DTC_RECORD_SIZE);
            memset(s->store.records[i + 1U], 0, OEM_DTC_RECORD_SIZE);
        }
    }
    s->scan_unused++;
    if (s->scan_unused >= OEM_DTC_SLOTS || s->scan_unused < s->store.count)
        s->scan_unused = s->store.count;
    if (s->scan_unused < OEM_DTC_SLOTS)
        memset(s->store.records[s->scan_unused], 0, OEM_DTC_RECORD_SIZE);
    if (!(s->gate & 32U)) {
        old_wait = s->lock_wait++;
        if (old_wait >= 20U) {
            s->gate |= 32U;
            s->lock_wait = 0;
        }
    } else
        s->lock_wait = 0;
    if ((u16)~s->clear_inverse != s->clear_request) {
        s->clear_request = 0;
        s->clear_inverse = 0xFFFFU;
        s->clear_wait = 0;
        s->clear_previous = 0;
    } else if ((u16)s->clear_previous != s->clear_request) {
        s->clear_previous = (u8)s->clear_request;
        s->clear_wait = 0;
    } else {
        old_wait = s->clear_wait++;
        if (old_wait >= 20U) {
            s->clear_request = 0;
            s->clear_inverse = 0xFFFFU;
            s->clear_wait = 0;
            s->clear_previous = 0;
        }
    }
    s->context[1] = 255U;
    return 1;
}
