/* Included by storage.c only in the stock-95080 profile. Two independent NOR
   erase sectors retain the previous tune until the new CRC/commit validates. */
void storage_load(void) {
    u8 slot, selected, valid[2] = {0, 0}, header[32];
    u16 at, error;
    u32 seq[2] = {0, 0};
    if (!storage_claim(&ecu.storage)) return;
    for (slot = 0; slot < 2; slot++) {
        if (!hal_cal_read(slot, 0, header, 32) || !valid_header(header)) continue;
        seq[slot] = get32(header + 8);
        valid[slot] = 1;
        for (at = 0; at < CAL_SIZE; at += 32) {
            if (!hal_cal_read(slot, (u16)(32U + at), ecu.cal.bytes[slot] + at, 32)) {
                valid[slot] = 0;
                break;
            }
            hal_watchdog_service();
        }
        if (valid[slot] && (crc16(0xFFFFU, ecu.cal.bytes[slot], CAL_SIZE) != get16(header + 12) ||
                           !cal_validate(ecu.cal.bytes[slot], &error))) valid[slot] = 0;
    }
    if (valid[0] || valid[1]) {
        selected = (u8)(valid[1] && (!valid[0] || (s32)(seq[1] - seq[0]) > 0));
        ecu.cal.active = selected;
        ecu.cal.valid = 1;
        ecu.cal.generation = 1;
        ecu.storage.active_slot = selected;
        ecu.storage.valid = 1;
        ecu.storage.sequence = seq[selected];
    }
    storage_release(&ecu.storage);
}
u8 storage_save(u32 now) {
    Storage *s = &ecu.storage;
    if (s->phase || ecu.cal.staging || !ecu.cal.valid || !storage_claim(s)) return 0;
    if (!service_enter(now)) { storage_release(s); return 0; }
    s->slot = s->valid ? (u8)(s->active_slot ^ 1U) : 0;
    s->generation = ecu.cal.generation;
    memset(s->header, -1, 32);
    memcpy(s->header, "LRC3", 4); /* Envelope ID; schema word is still 4. */
    put16(s->header + 4, CAL_SCHEMA);
    put16(s->header + 6, CAL_SIZE);
    put32(s->header + 8, s->sequence + 1UL);
    put16(s->header + 12, crc16(0xFFFFU, cal_active(), CAL_SIZE));
    put16(s->header + 14, s->generation);
    put16(s->header + 20, crc16(0xFFFFU, s->header, 20));
    s->offset = 0;
    s->phase = 1;
    s->result = 0;
    s->deadline = now + 10000UL;
    return 1;
}
void storage_poll(u32 now) {
    Storage *s = &ecu.storage;
    u8 commit[2];
    if (!s->phase) return;
    if (ecu.storage_owner != s) goto failed;
    if ((s32)(now - s->deadline) >= 0 || s->generation != ecu.cal.generation) {
        s->result = 2; s->phase = 0; storage_release(s); return;
    }
    switch (s->phase) {
    case 1:
        /* Finish the save-accepted reply before masking UART interrupts for
           erase. Status requests during erase may need a read-only retry. */
        if (ecu.protocol.tx_position < ecu.protocol.tx_length || !hal_uart_ready()) return;
        if (!hal_cal_erase(s->slot)) goto failed;
        s->phase = 2;
        break;
    case 2:
        if (!hal_cal_program(s->slot, (u16)(32U + s->offset), cal_active() + s->offset, 32)) goto failed;
        s->phase = 3;
        break;
    case 3:
        if (!hal_cal_read(s->slot, (u16)(32U + s->offset), s->verify, 32) ||
            memcmp(s->verify, cal_active() + s->offset, 32)) goto failed;
        s->offset += 32;
        s->phase = s->offset == CAL_SIZE ? 4 : 2;
        break;
    case 4:
        if (!hal_cal_program(s->slot, 0, s->header, 32)) goto failed;
        s->phase = 5;
        break;
    case 5:
        if (!hal_cal_read(s->slot, 0, s->verify, 32) || memcmp(s->verify, s->header, 32)) goto failed;
        s->phase = 6;
        break;
    case 6:
        commit[0] = 0xFF; commit[1] = COMMIT_BYTE;
        if (!hal_cal_program(s->slot, 30, commit, 2)) goto failed;
        s->phase = 7;
        break;
    case 7:
        if (!hal_cal_read(s->slot, 0, s->verify, 32) || !valid_header(s->verify)) goto failed;
        s->sequence = get32(s->header + 8);
        s->active_slot = s->slot;
        s->valid = 1; s->phase = 0; s->result = 1;
        ecu.cal.dirty = 0;
        storage_release(s);
        break;
    default: goto failed;
    }
    return;
failed:
    s->phase = 0; s->result = 3; storage_release(s);
}
