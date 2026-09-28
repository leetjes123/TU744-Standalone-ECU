#include "oem_history.h"
#include <string.h>

static u16 base(u8 slot) {
    return slot ? OEM_HISTORY_SLOT1 : OEM_HISTORY_SLOT0;
}
static u8 header_valid(const u8 *p) {
    return (u8)(p[0] == 'L' && p[1] == 'R' && p[2] == 'H' && p[3] == '1' && get16(p + 4) == OEM_HISTORY_SCHEMA &&
                get16(p + 6) == OEM_HISTORY_SIZE && get16(p + 20) == crc16(0xFFFFU, p, 20) &&
                p[31] == 0xA5U);
}
static u8 read_slot(OemHistory *h, u8 slot) {
    u16 at, start = base(slot);
    if (hal_eeprom_busy() != 0 || !hal_eeprom_read(start, h->header, 32))
        return OEM_HISTORY_IO;
    if (!header_valid(h->header))
        return OEM_HISTORY_INVALID;
    for (at = 0; at < OEM_HISTORY_SIZE; at += 32U) {
        if (!hal_eeprom_read((u16)(start + 32U + at), h->payload + at, 32))
            return OEM_HISTORY_IO;
        hal_watchdog_service(); /* Boot-only caller; outputs remain disabled. */
    }
    if (crc16(0xFFFFU, h->payload, OEM_HISTORY_SIZE) != get16(h->header + 12) ||
        !oem_history_validate(h->payload))
        return OEM_HISTORY_INVALID;
    return OEM_HISTORY_OK;
}
u8 oem_history_load(OemHistory *h, OemDiagnostics *s) {
    u8 slot, selected = 0, found = 0, result, io = 0;
    u32 sequence = 0, candidate;
    if (h->phase || !storage_claim(h))
        return 0;
    for (slot = 0; slot < HISTORY_SLOTS; slot++) {
        result = read_slot(h, slot);
        if (result == OEM_HISTORY_IO)
            io = 1;
        if (result != OEM_HISTORY_OK)
            continue;
        candidate = get32(h->header + 8);
        if (!found || (s32)(candidate - sequence) > 0) {
            selected = slot;
            sequence = candidate;
            found = 1;
        }
    }
    h->valid = 0;
    if (!found) {
        h->result = io ? OEM_HISTORY_IO : OEM_HISTORY_INVALID;
        storage_release(h);
        return 0;
    }
    result = read_slot(h, selected);
    if (result != OEM_HISTORY_OK || get32(h->header + 8) != sequence) {
        h->result = result == OEM_HISTORY_OK ? OEM_HISTORY_IO : result;
        storage_release(h);
        return 0;
    }
    if (!oem_history_decode(s, h->payload)) {
        h->result = OEM_HISTORY_INVALID;
        storage_release(h);
        return 0;
    }
    h->sequence = sequence;
    h->active_slot = selected;
    h->valid = 1;
    h->result = OEM_HISTORY_OK;
    storage_release(h);
    return 1;
}
u8 oem_history_save(OemHistory *h, const OemDiagnostics *s, u32 now) {
    if (h->phase || !storage_claim(h))
        return 0;
    if (!oem_history_encode(s, h->payload)) {
        h->result = OEM_HISTORY_INVALID;
        storage_release(h);
        return 0;
    }
    if (!service_enter(now)) {
        storage_release(h);
        return 0;
    }
    h->slot = (HISTORY_SLOTS > 1U && h->valid) ? (u8)(h->active_slot ^ 1U) : 0;
    memset(h->header, 0, 32);
    memcpy(h->header, "LRH1", 4);
    put16(h->header + 4, OEM_HISTORY_SCHEMA);
    put16(h->header + 6, OEM_HISTORY_SIZE);
    put32(h->header + 8, h->sequence + 1UL);
    put16(h->header + 12, crc16(0xFFFFU, h->payload, OEM_HISTORY_SIZE));
    put16(h->header + 20, crc16(0xFFFFU, h->header, 20));
    h->offset = 0;
    h->phase = 1;
    h->result = 0;
    h->deadline = now + 10000UL;
    return 1;
}
void oem_history_poll(OemHistory *h, u32 now) {
    u16 start = base(h->slot), address;
    u8 busy, commit;
    if (!h->phase)
        return;
    if (ecu.storage_owner != h)
        goto failed;
    if ((s32)(now - h->deadline) >= 0) {
        h->phase = 0;
        h->result = OEM_HISTORY_TIMEOUT;
        storage_release(h);
        return;
    }
    busy = hal_eeprom_busy();
    if (busy == 1)
        return;
    if (busy != 0)
        goto failed;
    switch (h->phase) {
    case 1:
        commit = 0;
        if (!hal_eeprom_write((u16)(start + 31), &commit, 1))
            goto failed;
        h->phase = 2;
        break;
    case 2:
        if (!hal_eeprom_read((u16)(start + 31), h->verify, 1) || h->verify[0])
            goto failed;
        h->phase = 3;
        break;
    case 3:
        address = (u16)(start + 32U + h->offset);
        if (!hal_eeprom_write(address, h->payload + h->offset, 32))
            goto failed;
        h->phase = 4;
        break;
    case 4:
        address = (u16)(start + 32U + h->offset);
        if (!hal_eeprom_read(address, h->verify, 32) ||
            memcmp(h->verify, h->payload + h->offset, 32))
            goto failed;
        h->offset += 32U;
        h->phase = h->offset == OEM_HISTORY_SIZE ? 5 : 3;
        break;
    case 5:
        if (!hal_eeprom_write(start, h->header, 32))
            goto failed;
        h->phase = 6;
        break;
    case 6:
        if (!hal_eeprom_read(start, h->verify, 32) || memcmp(h->verify, h->header, 32))
            goto failed;
        h->phase = 7;
        break;
    case 7:
        commit = 0xA5U;
        if (!hal_eeprom_write((u16)(start + 31), &commit, 1))
            goto failed;
        h->phase = 8;
        break;
    case 8:
        if (!hal_eeprom_read(start, h->verify, 32) || !header_valid(h->verify))
            goto failed;
        h->sequence = get32(h->header + 8);
        h->active_slot = h->slot;
        h->valid = 1;
        h->phase = 0;
        h->result = OEM_HISTORY_OK;
        storage_release(h);
        break;
    default:
        goto failed;
    }
    return;
failed:
    h->phase = 0;
    h->result = OEM_HISTORY_IO;
    storage_release(h);
}
