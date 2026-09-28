#include "board.h"
#include <string.h>
/* Protocol command 01: hand the K-line to the RAM firmware-update handler
   (FWUPDATE.A66), wire-compatible with the LRE-B4 handler. The caller has
   latched service mode and sent the accepted status. Nothing here returns
   to flash code once the handler starts; command 06 or a watchdog reset
   leaves it. */
extern const u8 far fw_update_start[], fw_update_end[];
/* Executable external SRAM, used only after the control core has stopped. */
static u16 update_ram[256];
typedef void (far *Handler)(void);
void hal_firmware_update(void) {
    u16 size = (u16)(fw_update_end - fw_update_start);
    u32 address;
    union {
        Handler call;
        struct { u16 offset, segment; } code;
    } handler;
    if (!size || size > sizeof(update_ram)) return;
    /* Full mask: no handler, including level-15 crank capture, may fetch from
       flash while it is erased. The handler never unmasks. */
    hal_hard_lock();
    hal_cancel_all();
    hal_aux(0, 0, 0, 0, 0, 0);
    memcpy(update_ram, fw_update_start, size);
    if (memcmp(update_ram, fw_update_start, size)) {
        __asm {SRST}
    }
    /* LRE-B4 raises P3.5 before its handler ("disable security module").
       OEM ROM evidence labels P3.5 the CC195 KTI test-pulse enable
       (board.h); the engine is stopped, so either reading is harmless here.
       The standalone NOR worker masks interrupts for a sector erase without
       it; the proven LRE-B4 sequence is kept until a board test decides. */
    PIN_KNOCK_KTI = 1;
    S0RIR = 0;
    /* C166 far DATA pointers are page:14-bit-offset, CODE pointers are
       segment:16-bit-offset (see flash_cal.c). */
    address = (u32)(void far *)update_ram;
    handler.code.offset = (u16)address;
    handler.code.segment = (u16)(address >> 16);
    handler.call();
    __asm {SRST}
}
