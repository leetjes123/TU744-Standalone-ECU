#include "board.h"
#if STOCK_95080
#include <string.h>
extern const u8 far flash_worker_start[], flash_worker_end[];
/* Executable external SRAM, with no shared use by journals or controller. */
static u16 worker_ram[192];
typedef u16 (far *Worker)(u16 page, u16 offset, u16 value, u16 operation);
static u8 copied;
static u8 valid(u8 slot, u16 at, u8 length) {
    return (u8)(slot < 2U && length && length <= 32U &&
                at < 32U + CAL_SIZE && length <= 32U + CAL_SIZE - at);
}
static u8 execute(u8 slot, u16 at, u16 value, u16 op) {
    u16 lock, result, size;
    u32 address;
    union {
        Worker call;
        struct { u16 offset, segment; } code;
    } worker;
    if (!ecu.service || ecu.rotation.rpm || ecu.rotation.state == ROT_VALID ||
        ecu.iac.off_pending || ecu.iac.holding) return 0;
    if (!copied) {
        size = (u16)(flash_worker_end - flash_worker_start);
        if (!size || size > sizeof(worker_ram)) return 0;
        memcpy(worker_ram, flash_worker_start, size);
        if (memcmp(worker_ram, flash_worker_start, size)) return 0;
        copied = 1;
    }
    /* C166 far DATA pointers are page:14-bit-offset, while CODE pointers
       are segment:16-bit-offset. A direct cast would jump into segment E0. */
    address = (u32)(void far *)worker_ram;
    /* Conversion through unsigned long normalizes the data pointer. */
    worker.code.offset = (u16)address;
    worker.code.segment = (u16)(address >> 16);
    /* Full mask: no handler, including level-15 crank capture, may fetch
       from flash while the SRAM worker programs it. */
    lock = hal_hard_lock();
    hal_cancel_all();
    result = worker.call(slot ? 0x18U : 0x14U, at, value, op);
    hal_hard_unlock(lock);
    return (u8)result;
}
u8 hal_cal_read(u8 slot, u16 at, u8 *bytes, u8 length) {
    const u8 far *source;
    if (!valid(slot, at, length)) return 0;
    source = (const u8 far *)(slot ? FLASH_CAL1 : FLASH_CAL0);
    memcpy(bytes, source + at, length);
    return 1;
}
u8 hal_cal_erase(u8 slot) {
    const u8 far *source;
    u16 at;
    if (slot > 1U || !execute(slot, 0, 0xFFFFU, 1)) return 0;
    source = (const u8 far *)(slot ? FLASH_CAL1 : FLASH_CAL0);
    for (at = 0; at < 32U + CAL_SIZE; at++)
        if (source[at] != 0xFFU) return 0;
    return 1;
}
u8 hal_cal_program(u8 slot, u16 at, const u8 *bytes, u8 length) {
    const u8 far *source;
    u16 value;
    u8 i;
    if (!valid(slot, at, length) || (at & 1U) || (length & 1U)) return 0;
    source = (const u8 far *)(slot ? FLASH_CAL1 : FLASH_CAL0);
    for (i = 0; i < length; i += 2) {
        if ((source[at + i] & bytes[i]) != bytes[i] ||
            (source[at + i + 1U] & bytes[i + 1U]) != bytes[i + 1U]) return 0;
        value = (u16)(bytes[i] | ((u16)bytes[i + 1U] << 8));
        if (!execute(slot, (u16)(at + i), value, 2)) return 0;
    }
    return 1;
}
#endif
