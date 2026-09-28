#include "control.h"
void fan_update(const u8 *c) {
    Controls *s = &ecu.control;
    Sensors *in = &ecu.sensors;
    if (in->clt.quality != QUALITY_VALID)
        s->fan_request = s->key_on;
    else if (in->clt.value >= (s8)c[0x5E2])
        s->fan_request = 1;
    else if (in->clt.value <= (s8)c[0x5E3])
        s->fan_request = 0;
}
static void fan_apply(u32 now, const u8 *c) {
    Controls *s = &ecu.control;
    u16 maximum = get16(c + CAL_IAC_MAX);
    if (!s->fan_request) {
        s->fan = s->fan_waiting = 0;
        return;
    }
    if (s->fan)
        return;
    /* The request already contributes idle feedforward before this decision.
       A failed/unavailable actuator must never prevent engine cooling. */
    if (ecu.sensors.clt.quality != QUALITY_VALID || !s->key_on || ecu.service ||
        s->mode != ENGINE_RUNNING || ecu.iac.state != IAC_READY || !c[0x8C0] ||
        (s->idle_mode != IDLE_FEEDBACK && s->idle_mode != IDLE_CATCH)) {
        s->fan = 1;
        s->fan_waiting = 0;
        return;
    }
    if (!s->fan_waiting) {
        s->fan_waiting = 1;
        s->fan_at = now;
        s->fan_target = s->idle_position;
    }
    if (s->fan_target > maximum)
        s->fan_target = maximum;
    if ((!ecu.iac.pending && ecu.iac.position >= s->fan_target) || now - s->fan_at >= 500UL) {
        s->fan = 1;
        s->fan_waiting = 0;
    }
}
void auxiliary_update(u32 now, const Rotation *r, const u8 *c) {
    Controls *s = &ecu.control;
    Sensors *in = &ecu.sensors;
    u8 heaters = 0;
    fan_apply(now, c);
    s->pump = (u8)(s->key_on && !ecu.service && ecu.cal.valid && ecu.board_released &&
                   ((s32)(s->prime_until - now) > 0 || r->state == ROT_VALID) &&
                   !(ecu.authority.inhibits & (INH_DEADLINE | INH_OUTPUT)));
    s->boost = 0;
    if ((c[0x5D4] & CFG_BOOST) && s->mode == ENGINE_RUNNING && in->map.quality == QUALITY_VALID &&
        in->tps.quality == QUALITY_VALID && in->tps.value > 100 &&
        in->map.value < get16(c + CAL_MAX_MAP) && !ecu.authority.inhibits &&
        !ecu.authority.plan.fuel_cut)
        s->boost = table2(c, 0x300, r->rpm, (u16)(in->tps.value / 10), 0x440);
    /* Legacy executable treats the table as HIGH percent despite its memloc
       comment saying LOW. Publish LOW percent to the HAL. */
    s->gauge =
        in->clt.quality == QUALITY_VALID ? (u8)(100U - table1(c, 0x5F0, in->clt.value, 0)) : 100;
    /* Owner-supplied wiring, 2026-09-22: in wideband mode the Spartan
       controller is powered across the OEM upstream-heater supply/ground
       conductors. Power it from key-on; its internal heater remains external. */
    if (c[0x600] && (c[CAL_FLAGS] & EQUIP_UPSTREAM_RELAY_HEATER) && ecu.key_input &&
        ecu.cal.valid && ecu.board_released && !ecu.service)
        heaters |= 1;
    if (s->mode == ENGINE_RUNNING && in->battery.quality == QUALITY_VALID && !ecu.service) {
        if (!c[0x600] && (c[CAL_FLAGS] & EQUIP_UPSTREAM_RELAY_HEATER))
            heaters |= 1;
        if (c[CAL_FLAGS] & EQUIP_DOWNSTREAM_RELAY_HEATER)
            heaters |= 2;
    }
    if (!safety_aux_permitted()) {
        s->pump = 0;
        s->boost = 0;
        heaters = 0;
    }
    hal_aux(s->pump, s->fan, s->boost, s->gauge, heaters, ecu.diagnostics.mil_output);
}
