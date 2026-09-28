#include "faults.h"
#include "oem_history.h"
#include <string.h>
#if STOCK_95080
#if FAULT_SLOT0 < (32U + OEM_HISTORY_SIZE) || FAULT_SLOT0 + FAULT_BYTES > FAULT_SLOT1 || \
    FAULT_SLOT1 + FAULT_BYTES > EEPROM_BYTES
#error Standalone fault journal overlaps stock history or exceeds EEPROM
#endif
#else
#if FAULT_SLOT0 < (32U + CAL_SIZE) || FAULT_SLOT0 + FAULT_BYTES > OEM_HISTORY_SLOT0 || \
    FAULT_SLOT1 < (4096U + 32U + CAL_SIZE) || FAULT_SLOT1 + FAULT_BYTES > OEM_HISTORY_SLOT1
#error Standalone fault journal overlaps calibration or native history
#endif
#endif
FaultState faults;
void faults_init(void) {
    memset(&faults, 0, sizeof(faults));
}
void fault_set(u8 id, u8 reason, u32 now) {
    FaultRecord *r;
    u16 mask;
    if (id >= FAULT_COUNT)
        return;
    mask = (u16)(1U << id);
    if (!reason) {
        faults.active &= (u16)~mask;
        return;
    }
    r = &faults.record[id];
    if (!(faults.active & mask) || r->reason != reason) {
        if (!(faults.stored & mask))
            r->first = now;
        if (r->occurrences < 65535U)
            r->occurrences++;
        r->last = now;
        r->reason = reason;
        faults.stored |= mask;
        faults.dirty = 1;
        if (faults.phase)
            faults.changed = 1;
    }
    faults.active |= mask;
}
static void sensor_fault(const Sensor *s, u8 id, u32 now, u8 monitor) {
    if (ecu.key_input)
        fault_set(id, monitor && s->quality != QUALITY_VALID ?
                  (s->quality ? s->quality : QUALITY_STALE) : 0, now);
}
void sensor_faults(u32 now) {
    Sensors *s = &ecu.sensors;
    /* Log measured input quality without changing any converted value. */
    sensor_fault(&s->tps, FAULT_TPS, now, 1);
    sensor_fault(&s->clt, FAULT_CLT, now, 1);
    sensor_fault(&s->iat, FAULT_IAT, now, 1);
    sensor_fault(&s->battery, FAULT_BATTERY, now, 1);
    sensor_fault(&s->map, FAULT_MAP, now, (u8)!(cal_active()[0x5D4] & CFG_ALPHA_N));
}
static u16 slot_base(u8 slot) { return slot ? FAULT_SLOT1 : FAULT_SLOT0; }
static u8 valid_image(const u8 *p) {
    u8 i;
    if (memcmp(p, "LRF1", 4) || p[4] != 1 || p[5] != FAULT_COUNT || p[6] || p[7] ||
        p[95] != 0xA5U || get16(p + 12) & 0xFFC0U || get16(p + 14) ||
        get16(p + 92) != crc16(0xFFFFU, p, 92) || p[94])
        return 0;
    for (i = 0; i < FAULT_COUNT; i++) {
        const u8 *r = p + 16U + 12U * i;
        if (r[1] || ((get16(p + 12) & (1U << i)) && (!r[0] || !get16(r + 2))))
            return 0;
    }
    return (u8)(get32(p + 88) == 0);
}
void faults_load(void) {
    u8 slot, i, found = 0, selected = 0, io = 0;
    u16 at;
    u32 sequence = 0, candidate;
    if (faults.loaded || faults.phase || !storage_claim(&faults))
        return;
    /* Called before any fault producers during target boot. Do not discard
       observations if a caller already started runtime acquisition. */
    if (faults.dirty) {
        storage_release(&faults);
        return;
    }
    for (slot = 0; slot < 2; slot++) {
        for (at = 0; at < FAULT_BYTES; at += 32U)
            if (hal_eeprom_busy() || !hal_eeprom_read((u16)(slot_base(slot) + at), faults.image + at, 32))
                break;
        if (at != FAULT_BYTES) { io = 1; continue; }
        if (!valid_image(faults.image))
            continue;
        candidate = get32(faults.image + 8);
        if (!found || (s32)(candidate - sequence) > 0) {
            found = 1; selected = slot; sequence = candidate;
        }
    }
    if (found) {
        for (at = 0; at < FAULT_BYTES; at += 32U)
            if (!hal_eeprom_read((u16)(slot_base(selected) + at), faults.image + at, 32))
                break;
        if (at != FAULT_BYTES || !valid_image(faults.image) || get32(faults.image + 8) != sequence)
            found = 0, io = 1;
    }
    faults.loaded = (u8)(found || !io);
    faults.result = io && !found ? 3 : 1;
    if (found) {
        faults.stored = get16(faults.image + 12);
        faults.sequence = sequence;
        faults.slot = selected;
        faults.valid = 1;
        for (i = 0; i < FAULT_COUNT; i++) {
            const u8 *r = faults.image + 16U + 12U * i;
            faults.record[i].reason = r[0];
            faults.record[i].occurrences = get16(r + 2);
            faults.record[i].first = get32(r + 4);
            faults.record[i].last = get32(r + 8);
        }
    }
    storage_release(&faults);
}
u8 faults_save(u32 now) {
    u8 i;
    if (!faults.loaded || !faults.dirty || faults.phase || ecu.key_input ||
        ecu.rotation.state != ROT_UNSYNCED || ecu.rotation.rpm || ecu.iac.off_pending ||
        !storage_claim(&faults))
        return 0;
    memset(faults.image, 0, FAULT_BYTES);
    memcpy(faults.image, "LRF1", 4);
    faults.image[4] = 1; faults.image[5] = FAULT_COUNT;
    put32(faults.image + 8, faults.sequence + 1UL);
    put16(faults.image + 12, faults.stored);
    for (i = 0; i < FAULT_COUNT; i++) {
        u8 *r = faults.image + 16U + 12U * i;
        r[0] = faults.record[i].reason;
        put16(r + 2, faults.record[i].occurrences);
        put32(r + 4, faults.record[i].first);
        put32(r + 8, faults.record[i].last);
    }
    put16(faults.image + 92, crc16(0xFFFFU, faults.image, 92));
    faults.target = faults.valid ? (u8)(faults.slot ^ 1U) : 0;
    faults.changed = 0;
    faults.offset = 0;
    faults.phase = 1;
    faults.result = 0;
    faults.deadline = now + 3000UL;
    return 1;
}
void faults_poll(u32 now) {
    u8 value, busy;
    u16 base = slot_base(faults.target);
    if (!faults.phase)
        return;
    if (ecu.storage_owner != &faults || (s32)(now - faults.deadline) >= 0)
        goto failed;
    busy = hal_eeprom_busy();
    if (busy == 1) return;
    if (busy) goto failed;
    switch (faults.phase) {
    case 1:
        value = 0;
        if (!hal_eeprom_write((u16)(base + 95), &value, 1)) goto failed;
        faults.phase = 2; break;
    case 2:
        if (!hal_eeprom_read((u16)(base + 95), &value, 1) || value) goto failed;
        faults.phase = 3; break;
    case 3:
        if (!hal_eeprom_write((u16)(base + faults.offset), faults.image + faults.offset, 32)) goto failed;
        faults.phase = 4; break;
    case 4:
        if (!hal_eeprom_read((u16)(base + faults.offset), faults.verify, 32) ||
            memcmp(faults.verify, faults.image + faults.offset, 32)) goto failed;
        faults.offset += 32;
        faults.phase = faults.offset == FAULT_BYTES ? 5 : 3; break;
    case 5:
        value = 0xA5;
        if (!hal_eeprom_write((u16)(base + 95), &value, 1)) goto failed;
        faults.phase = 6; break;
    case 6:
        if (!hal_eeprom_read((u16)(base + 64), faults.verify, 32)) goto failed;
        faults.image[95] = 0xA5;
        if (memcmp(faults.verify, faults.image + 64, 32)) goto failed;
        faults.slot = faults.target;
        faults.sequence++;
        faults.valid = 1;
        faults.dirty = faults.changed;
        faults.phase = 0;
        faults.result = 1;
        storage_release(&faults); break;
    default: goto failed;
    }
    return;
failed:
    faults.phase = 0;
    faults.result = 3;
    storage_release(&faults);
}
void faults_clear(void) {
    u8 i;
    /* A fault that is still present stays stored; it is re-evaluated at boot.
       The change is persisted at key-off like any new record. */
    for (i = 0; i < FAULT_COUNT; i++)
        if (!(faults.active & (1U << i)))
            memset(&faults.record[i], 0, sizeof(faults.record[i]));
    if (faults.stored != faults.active) {
        faults.stored = faults.active;
        faults.dirty = 1;
        if (faults.phase)
            faults.changed = 1;
    }
}
u8 faults_settled(void) { return (u8)(!faults.dirty && !faults.phase); }
