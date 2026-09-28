#include "ecu.h"
static const u8 phases[4] = {0x12, 0x13, 0x1B, 0x1A};
static void bridges_off(void) {
    u8 ignored;
    ecu.iac.off_pending = (u8)!hal_iac_transfer(0x3F, &ignored);
    ecu.iac.holding = 0;
    if (!ecu.iac.off_pending)
        ecu.iac.previous_active = 0;
}
static void fault(u8 reason) {
    Iac *s = &ecu.iac;
    if (!s->fault) {
        s->fault = reason;
        s->fault_count++;
    }
    s->state = IAC_FAULT;
    s->pending = 0;
    s->remaining = 0;
    bridges_off();
}
static u8 command(u8 value) {
    Iac *s = &ecu.iac;
    u8 response, diagnosis;
    if (s->holding) {
        /* Modulation repeats polarity; its latest high/low response cannot
           qualify another motion/open-load observation. */
        s->previous_active = 2;
        s->holding = 0;
    }
    if (!hal_iac_transfer(value, &response)) {
        diagnosis = hal_iac_hold_fault();
        if (diagnosis)
            s->response = hal_iac_hold_response();
        fault(diagnosis ? diagnosis : 1);
        return 0;
    }
    s->response = response;
    diagnosis = response & 0xC0U;
    if (s->previous_active) {
        if (diagnosis == 0 || diagnosis == 0x80U) {
            fault(diagnosis == 0x80U ? 3 : 4);
            return 0;
        }
        /* Low-current responses cannot qualify open-load recovery or failure.
           Count only responses following settled drive phases, preserving the
           count through intervening hold transfers. ST L9935 sections5.10-12. */
        if (s->previous_active == 1) {
            if (diagnosis == 0x40U) {
                if (++s->open_count >= 9) {
                    fault(2);
                    return 0;
                }
            } else
                s->open_count = 0;
        }
    }
    s->previous_active = value == 0x3FU ? 0 : ((value & 0x36U) == 0x24U ? 2 : 1);
    return 1;
}
void iac_disable(void) {
    ecu.iac.pending = 0;
    ecu.iac.remaining = 0;
    bridges_off();
    if (ecu.iac.off_pending)
        fault(6);
    else
        ecu.iac.state = ecu.iac.fault ? IAC_FAULT : IAC_UNKNOWN;
}
void iac_home(u32 now) {
    Iac *s = &ecu.iac;
    if (!ecu.cal.valid || !ecu.board_released || ecu.service ||
        ecu.control.mode != ENGINE_STOPPED || s->fault)
        return;
    s->state = IAC_UNKNOWN;
    if (!command(0x3F))
        return;
    s->open_count = 0;
    s->state = IAC_HOMING;
    s->remaining = get16(cal_active() + CAL_IAC_HOME_STEPS);
    if (!s->remaining)
        s->remaining = IAC_HOME_STEPS_DEFAULT;
    s->deadline = now + get16(cal_active() + CAL_HOME_MS);
    s->next_step = now;
    s->pending = 0;
}
void iac_service(u32 now) {
    Iac *s = &ecu.iac;
    u8 phase, closing;
    u8 hold_fault = hal_iac_hold_fault();
    if (hold_fault && !s->fault) {
        s->response = hal_iac_hold_response();
        fault(hold_fault);
        return;
    }
    if (s->off_pending) {
        if ((s32)(now - s->next_step) >= 0) {
            bridges_off();
            s->next_step = now + 10UL;
        }
        return;
    }
    if (s->state == IAC_FAULT || s->state == IAC_UNKNOWN)
        return;
    if (s->state == IAC_HOMING && (s32)(now - s->deadline) >= 0) {
        fault(5);
        return;
    }
    if ((s32)(now - s->next_step) < 0)
        return;
    if (s->pending) {
        /* Validate the response to the last energized phase before accounting
           for it. This is still commanded position, not a position sensor. */
        if (!command((u8)((phases[s->phase] & 0x09U) | 0x24U)))
            return;
        if (s->state == IAC_HOMING) {
            if (s->remaining)
                s->remaining--;
            if (!s->remaining) {
                s->state = IAC_READY;
                s->position = 0;
            }
        } else if (s->pending == 1)
            s->position++;
        else if (s->position)
            s->position--;
        s->pending = 0;
        if (!hal_iac_hold_start(phases[s->phase])) {
            hold_fault = hal_iac_hold_fault();
            fault(hold_fault ? hold_fault : 7);
            return;
        }
        s->holding = 1;
        s->next_step = now + 5UL;
        return;
    }
    if (s->state == IAC_READY && s->target == s->position)
        return;
    closing = (u8)(s->state == IAC_HOMING || s->target < s->position);
    /* Existing vehicle firmware closes by walking FORWARD through this table;
       opening walks backward. This is supplied vehicle evidence, not a newly
       inferred OEM electrical polarity. */
    phase = (u8)((s->phase + (closing ? 1U : 3U)) & 3U);
    if (!command(phases[phase]))
        return;
    s->phase = phase;
    s->pending = closing ? 2 : 1;
    s->next_step = now + 5UL;
}
