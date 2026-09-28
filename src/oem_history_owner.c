#include "oem_history.h"

u8 oem_history_owner_load(OemHistoryOwner *h, OemDiagnostics *s) {
    u8 restored;
    /* A boot retry must not discard pending work or reinterpret a stale result
       when another journal owns the device. No EEPROM access in that case. */
    if (h->ready || h->journal.phase || ecu.storage_owner)
        return 0;
    restored = oem_history_load(&h->journal, s);
    h->ready = (u8)(restored || h->journal.result == OEM_HISTORY_INVALID);
    h->dirty = (u8)!restored;
    return restored;
}

void oem_history_owner_changed(OemHistoryOwner *h) {
    h->dirty = 1;
    if (h->journal.phase)
        h->changed = 1;
}

void oem_history_owner_cleared(OemHistoryOwner *h) {
    oem_history_owner_changed(h);
    h->clear_pending = 1;
    h->clear_durable = 0;
    /* Any previous in-flight snapshot predates this completed clear. */
    h->clear_snapshot = 0;
}

u8 oem_history_owner_clear_request(OemHistoryOwner *h, OemDiagnostics *s, u8 kind, u8 event) {
    u8 accepted;
    if (!h->ready)
        return 0;
    if (kind == OEM_CLEAR_ALL)
        accepted = oem_dtc_clear_all(&s->events);
    else if (kind == OEM_CLEAR_EMISSIONS)
        accepted = oem_dtc_clear_emissions(&s->events);
    else if (kind == OEM_CLEAR_EVENT && event >= 1U && event <= 105U)
        accepted = oem_dtc_clear_event(&s->events, event);
    else
        return 0;
    if (!accepted)
        return 0;
    oem_diagnostics_bind(s);
    oem_history_owner_changed(h);
    return 1;
}

u8 oem_history_owner_clear_service(OemHistoryOwner *h, OemDiagnostics *s) {
    if (!h->ready || !oem_diagnostics_clear_ported(s))
        return 0;
    oem_history_owner_cleared(h);
    return 1;
}

u8 oem_history_owner_save(OemHistoryOwner *h, const OemDiagnostics *s, u32 now) {
    if (!h->ready || !h->dirty || !oem_history_save(&h->journal, s, now))
        return 0;
    h->changed = 0;
    h->clear_snapshot = h->clear_pending;
    return 1;
}

void oem_history_owner_poll(OemHistoryOwner *h, u32 now) {
    if (!h->journal.phase)
        return;
    oem_history_poll(&h->journal, now);
    if (h->journal.phase)
        return;
    if (h->journal.result == OEM_HISTORY_OK) {
        h->dirty = h->changed;
        if (h->clear_snapshot) {
            h->clear_pending = 0;
            h->clear_durable = 1;
        }
    }
    /* Failure preserves dirty/clear_pending for an explicit retry. There is no
       automatic write loop, clear acknowledgement, or power-latch operation. */
    h->clear_snapshot = 0;
}

u8 oem_history_owner_settled(const OemHistoryOwner *h) {
    return (u8)(h->ready && !h->journal.phase && !h->dirty && !h->clear_pending);
}
