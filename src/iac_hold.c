#include "iac_hold.h"

void iac_hold_fail(IacHold *s, u8 reason) {
    if (!s->fault) {
        s->fault = reason;
        s->fault_response = s->response;
    }
    s->enabled = 0;
    s->off_needed = 1;
}
u8 iac_hold_start(IacHold *s, u8 high, u16 now) {
    if (s->state != HOLD_IDLE || s->fault || s->off_needed || (high & 0xF6U) != 0x12U)
        return 0;
    s->enabled = 1;
    s->high = high;
    s->current = (u8)((high & 9U) | 0x24U); /* caller has just sent low */
    s->state = HOLD_WAIT;
    s->stamp = now;
    s->due = (u16)(now + HOLD_LOW_TICKS - HOLD_SPI_TICKS);
    return 1;
}
void iac_hold_stop(IacHold *s) {
    s->enabled = 0;
    if (s->state == HOLD_WAIT)
        s->state = HOLD_IDLE;
    /* An in-flight byte retains ownership until completion or timeout.
       Normal handoff keeps the phase; a fault requires an off transaction. */
}
u16 iac_hold_watch(IacHold *s, u16 now, u8 bus_busy) {
    u8 command;
    if (s->state == HOLD_RX) {
        if ((u16)(now - s->stamp) > HOLD_RX_TICKS) {
            iac_hold_fail(s, 1);
            s->state = HOLD_IDLE;
            return HOLD_ABORT; /* deselect interrupted frame before retry */
        }
        return HOLD_NONE;
    }
    if (s->state == HOLD_WAIT) {
        if ((s16)(now - s->due) < 0)
            return HOLD_NONE;
        if ((u16)(now - s->due) > HOLD_LATE_TICKS || bus_busy) {
            iac_hold_fail(s, 7);
            s->state = HOLD_IDLE;
        } else {
            command = s->current == s->high ? (u8)((s->high & 9U) | 0x24U) : s->high;
            s->pending = command;
            s->state = HOLD_RX;
            s->stamp = now;
            return command;
        }
    }
    if (s->off_needed && !bus_busy) {
        s->pending = 0x3F;
        s->state = HOLD_RX;
        s->stamp = now;
        return 0x3F;
    }
    return HOLD_NONE;
}
void iac_hold_complete(IacHold *s, u8 response, u16 now) {
    u8 diagnosis = response & 0xC0U;
    if (s->state != HOLD_RX)
        return; /* stale completion cannot resurrect an aborted session */
    s->response = response;
    if ((u16)(now - s->stamp) > HOLD_RX_TICKS)
        iac_hold_fail(s, 1);
    if (s->current != 0x3FU && (diagnosis == 0 || diagnosis == 0x80U))
        iac_hold_fail(s, diagnosis == 0x80U ? 3 : 4);
    /* Repeated same-polarity hold changes do not qualify open-load checks.
       The foreground still checks one response per settled motion phase. */
    s->current = s->pending;
    s->state = HOLD_IDLE;
    if (s->pending == 0x3FU) {
        s->off_needed = 0;
        s->enabled = 0;
    } else if (s->enabled && !s->fault) {
        s->state = HOLD_WAIT;
        s->stamp = now;
        s->due = (u16)(now + (s->current == s->high ? HOLD_HIGH_TICKS : HOLD_LOW_TICKS) -
                       HOLD_SPI_TICKS);
    }
}
