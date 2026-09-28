#include "board.h"
#include "iac_hold.h"
#define SSC_FREE 0U
#define SSC_FOREGROUND 1U
#define SSC_HOLD 2U
/* One serialized owner. A hold session reserves SSC through both WAIT/RX;
   foreground must stop/drain it before a normal IAC byte. EEPROM is rejected
   while held. RX/compare run above T6; every owner mutation raises ILVL14,
   serializing those handlers with tick/foreground calls. No interrupt handler
   polls for byte completion. */
static volatile u8 owned;
static volatile u8 last_iac_command = 0x3F;
static IacHold hold;
static u16 progress_clock;
static u32 progress_stamp;
static u8 bus_error(void) {
    return (u8)(SSCTE || SSCRE || SSCPE || SSCBE);
}
static void clear_errors(void) {
    /* Bit writes do not restore an obsolete hardware-updated BSY value. */
    SSCTE = 0;
    SSCRE = 0;
    SSCPE = 0;
    SSCBE = 0;
}
static void disarm(void) {
    CC22IE = 0;
    CCM5 &= 0xF0FFU;
    CC22IR = 0;
}
static void abort_frame(void) {
    SSCRIE = 0;
    SSCRIR = 0;
    PIN_IAC_SELECT = 1;
    clear_errors();
}
static void received(void) {
    u8 response = (u8)SSCRB;
    SSCRIR = 0;
    SSCRIE = 0;
    PIN_IAC_SELECT = 1;
    last_iac_command = hold.pending;
    hold.response = response;
    if (bus_error()) {
        clear_errors();
        iac_hold_fail(&hold, 1);
    }
    iac_hold_complete(&hold, response, T7);
}
/* Caller owns interrupt exclusion. RX IRQ entry consumes its request flag;
   tick/foreground polling instead checks the still-latched SSCRIR. */
static void pump(void) {
    u16 action;
    if (owned != SSC_HOLD)
        return;
    if (hold.state == HOLD_RX && SSCRIR)
        received();
    action = iac_hold_watch(&hold, T7, (u8)((SSCCON & 0x1000U) != 0));
    if (action == HOLD_ABORT) {
        abort_frame();
    } else if (action < HOLD_NONE) {
        PIN_EEPROM_SELECT = 1;
        PIN_IAC_SELECT = 0;
        SSCRIR = 0;
        SSCRIE = 1;
        SSCTB = action;
    }
    if (hold.state == HOLD_WAIT) {
        CC22 = hold.due;
        CC22IR = 0;
        CCM5 = (CCM5 & 0xF0FFU) | 0x0400U;
        CC22IE = 1;
        /* T7 continues while interrupts are masked. The compare can pass
           after watch() sampled it and before CC22IR was cleared above.
           Restore a pending request so that handoff cannot lose the event. */
        if ((s16)(T7 - hold.due) >= 0)
            CC22IR = 1;
    } else
        disarm();
    if (hold.state == HOLD_IDLE && !hold.off_needed) {
        SSCRIE = 0;
        owned = SSC_FREE;
    }
}
void board_ssc_tick(void) {
    u16 lock = hal_lock();
    /* A normal drive phase also needs supervision if foreground stops before
       it can enter hold. Never interrupt a foreground SSC frame mid-byte. */
    if (owned == SSC_FREE && last_iac_command != 0x3FU &&
        ((ecu.authority.inhibits & (INH_DEADLINE | INH_OUTPUT)) || ecu.service || !ecu.key_input)) {
        hold.current = last_iac_command;
        hold.state = HOLD_IDLE;
        hold.enabled = 0;
        hold.off_needed = 1;
        owned = SSC_HOLD;
        progress_clock = T7;
        progress_stamp = ecu.milliseconds;
    }
    if (owned == SSC_HOLD) {
        if (ecu.milliseconds - progress_stamp >= 2UL) {
            /* T1-derived milliseconds also supervise a stopped T7 counter. */
            if (T7 == progress_clock) {
                iac_hold_fail(&hold, 7);
                iac_hold_stop(&hold);
                if (hold.state == HOLD_RX) {
                    abort_frame();
                    hold.state = HOLD_IDLE;
                }
            }
            progress_clock = T7;
            progress_stamp = ecu.milliseconds;
        }
        if (ecu.authority.inhibits & (INH_DEADLINE | INH_OUTPUT)) {
            iac_hold_fail(&hold, 7);
            iac_hold_stop(&hold);
        } else if (ecu.service || !ecu.key_input) {
            hold.off_needed = 1;
            iac_hold_stop(&hold);
        }
        pump();
    }
    hal_unlock(lock);
}
void iac_compare_isr(void) IRQ_HANDLER(0x36) {
    u16 lock = hal_lock();
    pump();
    hal_unlock(lock);
}
void ssc_receive_isr(void) IRQ_HANDLER(0x2E) {
    u16 lock = hal_lock();
    if (owned == SSC_HOLD && hold.state == HOLD_RX) {
        received();
        pump();
    }
    hal_unlock(lock);
}
static u8 stop_hold(void) {
    u16 count = 512, lock = hal_lock();
    if (owned == SSC_HOLD) {
        iac_hold_stop(&hold);
        disarm();
        pump();
    }
    hal_unlock(lock);
    /* Bounded foreground handoff. Polling also works with interrupts disabled
       by the caller; no second device is selected during an in-flight byte. */
    while (owned == SSC_HOLD && --count)
        board_ssc_tick();
    return (u8)(owned == SSC_FREE);
}
u8 hal_iac_hold_fault(void) {
    return hold.fault; /* atomic byte, first cause retained until reset */
}
u8 hal_iac_hold_response(void) {
    return hold.fault_response;
}
u8 hal_iac_hold_start(u8 high_command) {
    u8 ok = 0;
    u16 lock = hal_lock();
    if (BOARD_RELEASED && owned == SSC_FREE && !(SSCCON & 0x1000U) &&
        iac_hold_start(&hold, high_command, T7)) {
        owned = SSC_HOLD;
        progress_clock = T7;
        progress_stamp = ecu.milliseconds;
        pump();
        ok = 1;
    }
    hal_unlock(lock);
    return ok;
}
static u8 transfer(u8 tx, u8 *rx) {
    u16 count = 2000;
    if (bus_error()) {
        clear_errors();
        return 0;
    }
    while ((SSCCON & 0x1000U) && --count) {
    }
    if (!count)
        return 0;
    SSCRIR = 0;
    SSCTB = tx;
    count = 2000;
    while (!SSCRIR && --count) {
    }
    if (!count)
        return 0;
    *rx = (u8)SSCRB;
    if (bus_error()) {
        clear_errors();
        return 0;
    }
    return 1;
}
static u8 claim(void) {
    u16 lock = hal_lock();
    if (owned) {
        hal_unlock(lock);
        return 0;
    }
    owned = SSC_FOREGROUND;
    PIN_EEPROM_SELECT = 1;
    PIN_IAC_SELECT = 1;
    hal_unlock(lock);
    return 1;
}
static void release(void) {
    u16 lock = hal_lock();
    PIN_EEPROM_SELECT = 1;
    PIN_IAC_SELECT = 1;
    owned = SSC_FREE;
    hal_unlock(lock);
}
u8 hal_iac_transfer(u8 command, u8 *response) {
    u8 ok;
    if (!stop_hold() || (hold.fault && command != 0x3FU) || !claim())
        return 0;
    PIN_IAC_SELECT = 0;
    if (command != 0x3FU)
        last_iac_command = command; /* conservatively active even if RX fails */
    ok = transfer(command, response);
    if (ok)
        last_iac_command = command;
    release();
    return ok;
}
u8 hal_eeprom_read(u16 at, u8 *bytes, u8 length) {
    u8 ignored, i, ok = 1;
    if (!length || length > 32 || at > EEPROM_BYTES || length > EEPROM_BYTES - at || !claim())
        return 0;
    PIN_EEPROM_SELECT = 0;
    if (!transfer(3, &ignored) || !transfer((u8)(at >> 8), &ignored) || !transfer((u8)at, &ignored))
        ok = 0;
    for (i = 0; i < length && ok; i++)
        ok = transfer(0xFF, bytes + i);
    release();
    return ok;
}
u8 hal_eeprom_busy(void) {
    u8 response, ok;
    if (!claim())
        return 2;
    PIN_EEPROM_SELECT = 0;
    ok = transfer(5, &response) && transfer(0xFF, &response);
    release();
    return ok ? (response & 1U) : 2;
}
u8 hal_eeprom_write(u16 at, const u8 *bytes, u8 length) {
    u8 response, i, ok;
    if (!length || length > 32 || at > EEPROM_BYTES || length > EEPROM_BYTES - at || (at & 31U) + length > 32 ||
        hal_eeprom_busy() != 0 || !claim())
        return 0;
    PIN_EEPROM_SELECT = 0;
    ok = transfer(6, &response);
    PIN_EEPROM_SELECT = 1;
    PIN_EEPROM_SELECT = 0;
    ok = (u8)(ok && transfer(2, &response) && transfer((u8)(at >> 8), &response) &&
              transfer((u8)at, &response));
    for (i = 0; i < length && ok; i++)
        ok = transfer(bytes[i], &response);
    release();
    return ok;
}
