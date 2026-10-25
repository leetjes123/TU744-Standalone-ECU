#include "ecu.h"
/* OEM drive bytes (file 0x15AD6 = 00,08,09,01): polarity bits only, current
   code 00 on both bridges, walked in the same cycle as before. The OEM sends
   nothing at target, so the last phase stays energized at drive current. */
static const u8 phases[4] = {0x00, 0x01, 0x09, 0x08};
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
        s->last_fault = reason;
        s->fault_count++;
    }
    s->retry_armed = 0;
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
static u8 recovery_permitted(void) {
    return (u8)(ecu.cal.valid && ecu.board_released && !ecu.service && !ecu.iac.off_pending &&
                !(ecu.authority.inhibits & (INH_DEADLINE | INH_OUTPUT)));
}
/* Drop a latched driver fault once the hold session has released the bus.
   last_fault, fault_count and the DTC monitor history keep the evidence. */
static u8 clear_fault(void) {
    Iac *s = &ecu.iac;
    if (!hal_iac_hold_reset())
        return 0;
    s->fault = 0;
    s->open_count = 0;
    s->pending = 0;
    s->remaining = 0;
    s->previous_active = 0;
    s->holding = 0;
    s->retry_armed = 0;
    return 1;
}
static void start_homing(u32 now) {
    Iac *s = &ecu.iac;
    s->open_count = 0;
    s->state = IAC_HOMING;
    s->remaining = get16(cal_active() + CAL_IAC_HOME_STEPS);
    if (!s->remaining)
        s->remaining = IAC_HOME_STEPS_DEFAULT;
    s->deadline = now + get16(cal_active() + CAL_HOME_MS);
    s->next_step = now;
    s->pending = 0;
    s->last_direction = 0;
    s->resyncs = 0;
}
void iac_home(u32 now) {
    Iac *s = &ecu.iac;
    if (!ecu.cal.valid || !ecu.board_released || ecu.service ||
        ecu.control.mode != ENGINE_STOPPED)
        return;
    /* Key-on is a fresh reference: a fault from the last drive is retried. */
    if (s->fault && (!recovery_permitted() || !clear_fault()))
        return;
    s->state = IAC_UNKNOWN;
    if (!command(0x3F))
        return;
    start_homing(now);
}
/* A single disturbance (sync loss, SPI error, late hold service) must not
   leave idle control frozen until reset. Retry IAC_RETRY_MS after the fault,
   then no more often than every IAC_RETRY_SPACING_MS. Running: resume with
   the counted position, as homing would close the valve under a running
   engine. Stopped: re-reference by homing. */
static void recover(u32 now) {
    Iac *s = &ecu.iac;
    if (!s->retry_armed) {
        s->retry_armed = 1;
        s->retry_at = now + IAC_RETRY_MS;
        return;
    }
    if ((s32)(now - s->retry_at) < 0 ||
        (s->retries && now - s->retried_at < IAC_RETRY_SPACING_MS) ||
        !ecu.control.key_on || !recovery_permitted() || !clear_fault())
        return;
    if (s->retries != 255U)
        s->retries++;
    s->retried_at = now;
    if (!command(0x3F))
        return;
    if (ecu.control.mode == ENGINE_STOPPED) {
        start_homing(now);
        return;
    }
    s->state = IAC_READY;
    s->next_step = now;
    s->last_direction = 0;
}
/* OEM position re-sync (0x41258 counter 848C, 0x41040 FD28.3 edge): after
   5 s of qualified idle at least 120 rpm above target, the counted position
   gains 5, so the valve is driven 5 steps further closed than the count
   says. At most 40 per key cycle (92BF, file 0x10FB0). The caller qualifies
   with target <= position instead of the OEM's controller-state compare
   9762 == 9746 and its not-at-target test, so it also acts at closed. */
void iac_resync(u32 now, u8 qualified) {
    Iac *s = &ecu.iac;
    if (!qualified || s->state != IAC_READY) {
        s->resync_since = now;
        return;
    }
    if (now - s->resync_since < IAC_RESYNC_MS)
        return;
    s->resync_since = now;
    if (s->resyncs >= IAC_RESYNC_LIMIT)
        return;
    s->position = (u16)(s->position + IAC_RESYNC_COUNTS);
    s->resyncs++;
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
    if (s->state == IAC_FAULT) {
        recover(now);
        return;
    }
    if (s->state == IAC_UNKNOWN)
        return;
    if (s->state == IAC_HOMING && (s32)(now - s->deadline) >= 0) {
        fault(5);
        return;
    }
    if ((s32)(now - s->next_step) < 0)
        return;
    if (s->pending) {
        /* Validate the response to the last energized phase before accounting
           for it, re-sending the same byte so the coil current is unchanged.
           This is still commanded position, not a position sensor. */
        if (!command(phases[s->phase]))
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
        if (s->state == IAC_HOMING) {
            s->next_step = now + IAC_STEP_MS; /* homing keeps its 10 ms cadence */
            return;
        }
    }
    if (s->state == IAC_READY && s->target == s->position)
        return;
    closing = (u8)(s->state == IAC_HOMING || s->target < s->position);
    /* OEM 0x41040: a step against the previous direction waits five driver
       calls (counters 92BB/92BC), 25 ms at the OEM 5 ms step call. */
    if (s->last_direction && s->last_direction != (closing ? 2U : 1U) &&
        (s32)(now - s->reverse_at) < 0)
        return;
    /* Existing vehicle firmware closes by walking FORWARD through this table;
       opening walks backward. The OEM walks the same cycle: one count is one
       table step (0x411A4), so counts are physical full steps. */
    phase = (u8)((s->phase + (closing ? 1U : 3U)) & 3U);
    if (!command(phases[phase]))
        return;
    s->phase = phase;
    s->pending = closing ? 2 : 1;
    s->last_direction = s->pending;
    s->reverse_at = now + IAC_REVERSE_MS;
    /* OEM step call: every 5th ADC completion, every 4th after opening. */
    s->next_step = now + (closing ? IAC_STEP_MS : IAC_OPEN_STEP_MS);
}
