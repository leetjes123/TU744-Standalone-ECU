#include "ecu.h"
#include <string.h>
#define SLOT_SIZE 4096U
#define PAYLOAD_OFFSET 32U
#define COMMIT_BYTE 0xA5U
u8 storage_claim(const void *owner) {
    if (!owner || ecu.storage_owner)
        return 0;
    ecu.storage_owner = owner;
    return 1;
}
void storage_release(const void *owner) {
    if (ecu.storage_owner == owner)
        ecu.storage_owner = 0;
}
static u8 valid_header(const u8 *h) {
    return (u8)(h[0] == 'L' && h[1] == 'R' && h[2] == 'C' && h[3] == '3' &&
                get16(h + 4) == CAL_SCHEMA && get16(h + 6) == CAL_SIZE &&
                get16(h + 20) == crc16(0xFFFFU, h, 20) && h[31] == COMMIT_BYTE);
}
#if !STOCK_95080
void storage_load(void) {
    u8 slot, valid[2] = {0, 0};
    u16 at, error;
    u32 seq[2] = {0, 0};
    u8 header[32];
    if (!storage_claim(&ecu.storage))
        return;
    for (slot = 0; slot < 2; slot++) {
        if (!hal_eeprom_read((u16)(slot * SLOT_SIZE), header, 32) || !valid_header(header))
            continue;
        valid[slot] = 1;
        seq[slot] = get32(header + 8);
        for (at = 0; at < CAL_SIZE; at += 32) {
            if (!hal_eeprom_read((u16)(slot * SLOT_SIZE + PAYLOAD_OFFSET + at),
                                 ecu.cal.bytes[slot] + at, 32)) {
                valid[slot] = 0;
                break;
            }
            hal_watchdog_service(); /* Boot only, interrupts/output authority still off. */
        }
        if (valid[slot] && (crc16(0xFFFFU, ecu.cal.bytes[slot], CAL_SIZE) != get16(header + 12) ||
                            !cal_validate(ecu.cal.bytes[slot], &error)))
            valid[slot] = 0;
    }
    if (!valid[0] && !valid[1]) {
        storage_release(&ecu.storage);
        return;
    }
    slot = (u8)(valid[1] && (!valid[0] || (s32)(seq[1] - seq[0]) > 0));
    ecu.cal.active = slot;
    ecu.cal.valid = 1;
    ecu.cal.generation = 1;
    ecu.storage.active_slot = slot;
    ecu.storage.valid = 1;
    ecu.storage.sequence = seq[slot];
    storage_release(&ecu.storage);
}
u8 storage_save(u32 now) {
    Storage *s = &ecu.storage;
    if (s->phase || ecu.cal.staging || !ecu.cal.valid || !storage_claim(s))
        return 0;
    if (!service_enter(now)) {
        storage_release(s);
        return 0;
    }
    s->slot = s->valid ? (u8)(s->active_slot ^ 1U) : 0;
    s->generation = ecu.cal.generation;
    s->sequence++;
    memset(s->header, 0, 32);
    s->header[0] = 'L';
    s->header[1] = 'R';
    s->header[2] = 'C';
    s->header[3] = '3';
    put16(s->header + 4, CAL_SCHEMA);
    put16(s->header + 6, CAL_SIZE);
    put32(s->header + 8, s->sequence);
    put16(s->header + 12, crc16(0xFFFFU, cal_active(), CAL_SIZE));
    put16(s->header + 14, s->generation);
    put16(s->header + 20, crc16(0xFFFFU, s->header, 20));
    s->header[31] = 0;
    s->phase = 1;
    s->offset = 0;
    s->result = 0;
    s->deadline = now + 10000UL;
    return 1;
}
void storage_poll(u32 now) {
    Storage *s = &ecu.storage;
    u16 base = (u16)(s->slot * SLOT_SIZE), address;
    u8 busy, commit;
    if (!s->phase)
        return;
    if (ecu.storage_owner != s)
        goto failed;
    if ((s32)(now - s->deadline) >= 0 || s->generation != ecu.cal.generation) {
        s->result = 2;
        s->phase = 0;
        storage_release(s);
        return;
    }
    busy = hal_eeprom_busy();
    if (busy == 1)
        return;
    if (busy == 2) {
        s->result = 3;
        s->phase = 0;
        storage_release(s);
        return;
    }
    switch (s->phase) {
    case 1: /* Invalidate only the inactive slot, then verify that invalidation. */
        commit = 0;
        if (!hal_eeprom_write((u16)(base + 31), &commit, 1))
            goto failed;
        s->phase = 2;
        break;
    case 2:
        if (!hal_eeprom_read((u16)(base + 31), s->verify, 1) || s->verify[0] != 0)
            goto failed;
        s->phase = 3;
        break;
    case 3:
        address = (u16)(base + PAYLOAD_OFFSET + s->offset);
        if (!hal_eeprom_write(address, cal_active() + s->offset, 32))
            goto failed;
        s->phase = 4;
        break;
    case 4:
        address = (u16)(base + PAYLOAD_OFFSET + s->offset);
        if (!hal_eeprom_read(address, s->verify, 32) ||
            memcmp(s->verify, cal_active() + s->offset, 32))
            goto failed;
        s->offset += 32;
        s->phase = s->offset == CAL_SIZE ? 5 : 3;
        break;
    case 5:
        if (!hal_eeprom_write(base, s->header, 32))
            goto failed;
        s->phase = 6;
        break;
    case 6:
        if (!hal_eeprom_read(base, s->verify, 32) || memcmp(s->verify, s->header, 32))
            goto failed;
        s->phase = 7;
        break;
    case 7:
        commit = COMMIT_BYTE;
        if (!hal_eeprom_write((u16)(base + 31), &commit, 1))
            goto failed;
        s->phase = 8;
        break;
    case 8:
        if (!hal_eeprom_read(base, s->verify, 32) || !valid_header(s->verify))
            goto failed;
        s->phase = 0;
        s->result = 1;
        s->active_slot = s->slot;
        s->valid = 1;
        ecu.cal.dirty = 0;
        storage_release(s);
        break;
    default:
        goto failed;
    }
    return;
failed:
    s->result = 3;
    s->phase = 0;
    storage_release(s);
}
#else
/* Keep header framing/CRC common, but no calibration traffic uses EEPROM. */
#include "storage_flash.inc"
#endif
