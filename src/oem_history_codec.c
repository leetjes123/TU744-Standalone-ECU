#include "oem_history.h"
#include "oem_layout.h"
#include <string.h>

static const u8 image_id[32] = {0x57, 0x10, 0x01, 0x5F, 0x7C, 0x5C, 0x06, 0x6C, 0x86, 0x0A, 0x17,
                                0x57, 0xFD, 0x89, 0x3F, 0x30, 0x57, 0x01, 0x60, 0x8C, 0x25, 0xAF,
                                0x8F, 0xF2, 0x3B, 0xFC, 0xB4, 0xFD, 0x1E, 0x48, 0x37, 0xB3};
static void native_word(u8 *p, u16 value) {
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}
static u16 read_word(const u8 *p) {
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}
u8 oem_history_validate(const u8 *p) {
    u8 i, count = p[40];
    u16 at;
    if (memcmp(p, image_id, 32) || count > OEM_DTC_SLOTS || p[755] != OEM_HISTORY_SCHEMA)
        return 0;
    for (i = 0; i < count; i++)
        if (p[53U + 24U * i] >= 107U || p[58U + 24U * i] >= 38U)
            return 0;
    for (at = 756; at < OEM_HISTORY_SIZE; at++)
        if (p[at])
            return 0;
    return 1;
}
u8 oem_history_encode(const OemDiagnostics *s, u8 *p) {
    u8 i;
    const OemDtcState *d = &s->events;
    if (d->store.count > OEM_DTC_SLOTS)
        return 0;
    for (i = 0; i < d->store.count; i++)
        if (d->store.records[i][0] >= 107U || d->store.records[i][5] >= 38U)
            return 0;
    memset(p, 0, OEM_HISTORY_SIZE);
    memcpy(p, image_id, 32);
    /* Native retained address spans, in ascending order. The normal boot
       clear ends at38293E; none of the volatile flags/filters is serialized. */
    p[32] = (u8)s->coolant.value[OEM_CLT_AA5C];
    p[33] = s->iat.captured;
    p[34] = d->store.phases[0];
    p[35] = d->store.phases[6];
    p[36] = d->scan_record;
    p[37] = d->scan_unused;
    p[38] = d->scan_event;
    p[39] = d->last_event;
    p[40] = d->store.count;
    p[41] = d->overflow;
    p[42] = d->context[1];
    p[43] = d->warmup_count;
    p[44] = s->mil.retained;
    native_word(p + 45, s->context.value[9]); /* ABB6 */
    native_word(p + 47, s->coolant.value[OEM_CLT_B03E]);
    native_word(p + 49, d->drive_count);
    p[51] = d->store.active_demand;
    p[52] = d->store.demand;
    memcpy(p + 53, d->store.records, 480);
    for (i = 0; i < 107U; i++)
        native_word(p + 533U + 2U * i, d->live[i]);
    native_word(p + 747, d->timestamp);
    /* Schema2 extension: retain AA68..AA6D without shifting the schema1
       field offsets. Old snapshots are rejected, never interpreted as proof
       that newly represented monitors have completed. */
    memcpy(p + 749, s->readiness.count, 5);
    p[754] = s->readiness.pending;
    p[755] = OEM_HISTORY_SCHEMA;
    return 1;
}
u8 oem_history_decode(OemDiagnostics *s, const u8 *p) {
    u8 i;
    OemDtcState *d = &s->events;
    if (!oem_history_validate(p))
        return 0;
    s->coolant.value[OEM_CLT_AA5C] = p[32];
    s->iat.captured = p[33];
    d->store.phases[0] = p[34];
    d->store.phases[6] = p[35];
    d->scan_record = p[36];
    d->scan_unused = p[37];
    d->scan_event = p[38];
    d->last_event = p[39];
    d->store.count = p[40];
    d->overflow = p[41];
    d->context[1] = p[42];
    d->warmup_count = p[43];
    s->mil.retained = p[44];
    s->context.value[9] = read_word(p + 45);
    s->coolant.value[OEM_CLT_B03E] = read_word(p + 47);
    d->drive_count = read_word(p + 49);
    d->store.active_demand = p[51];
    d->store.demand = p[52];
    memcpy(d->store.records, p + 53, 480);
    for (i = 0; i < 107U; i++)
        d->live[i] = read_word(p + 533U + 2U * i);
    d->timestamp = read_word(p + 747);
    memcpy(s->readiness.count, p + 749, 5);
    s->readiness.pending = p[754];
    oem_diagnostics_bind(s);
    return 1;
}
