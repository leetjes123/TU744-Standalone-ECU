#include "lifecycle.h"
VehicleState vehicle;

void launch_disarm(void) {
    vehicle.armed = vehicle.stationary = 0;
    vehicle.antilag = 0;
}
u8 launch_arm(u8 stationary, u32 now) {
    const u8 *c = cal_active();
    u16 lock, count;
    u32 edge;
    if (stationary > 1U || !ecu.cal.valid || !ecu.key_input || ecu.service ||
        !(c[0x5D4] & CFG_LAUNCH) || !get16(c + CAL_LAUNCH_MS) ||
        ecu.sensors.tps.quality != QUALITY_VALID || ecu.sensors.tps.value > 20 ||
        ecu.authority.inhibits & (INH_DEADLINE | INH_OUTPUT | INH_POWER))
        return 0;
    lock = hal_lock();
    count = ecu.vss_count;
    edge = ecu.vss_edge_stamp;
    hal_unlock(lock);
    if (stationary) {
        if (ecu.sensors.speed_kph || now - edge < 1000UL)
            return 0;
    } else if (!ecu.sensors.vss_valid || ecu.sensors.speed_kph >= c[0x7AF])
        return 0;
    vehicle.armed = 1;
    vehicle.stationary = stationary;
    vehicle.armed_at = now;
    vehicle.generation = ecu.cal.generation;
    vehicle.count_at_arm = count;
    vehicle.reason = 0;
    vehicle.antilag_used = vehicle.antilag_started = 0;
    return 1;
}

void antilag_update(u32 now, const Rotation *r, EnginePlan *p) {
    const u8 *c = cal_active();
    u16 start = get16(c + CAL_LAUNCH_SOFT_RPM);
    vehicle.antilag = 0;
    if (!start || !(c[0x7B4] & 8U))
        start = get16(c + 0x5E7);
    if (vehicle.antilag_started && now - vehicle.antilag_at >= get16(c + CAL_ANTILAG_MS))
        vehicle.antilag_used = 1;
    if (!(c[0x7B4] & 0x40U) || !ecu.control.launch || vehicle.antilag_used ||
        !get16(c + CAL_ANTILAG_MS) || r->rpm < start || ecu.control.rev_limited ||
        (p->fuel_cut & (CUT_BOOST | CUT_REV | CUT_DFCO | CUT_FLOOD)) ||
        ecu.sensors.clt.quality != QUALITY_VALID || ecu.sensors.iat.quality != QUALITY_VALID ||
        ecu.sensors.clt.value < 60 || ecu.sensors.clt.value > c[CAL_ANTILAG_CLT] ||
        ecu.sensors.iat.value > c[CAL_ANTILAG_IAT])
        return;
    if (!vehicle.antilag_started) {
        vehicle.antilag_started = 1;
        vehicle.antilag_at = now;
    }
    vehicle.antilag = 1;
    /* Override only launch's hard/soft cut contributions. A main limiter or
       other protection always wins and is never cleared by this overlay. */
    if (c[0x7B4] & 0x80U) {
        if (p->fuel_cut & CUT_LAUNCH)
            p->spark_cut |= CUT_LAUNCH;
        p->fuel_cut &= 0xFFF7U;
        p->soft_fuel = vehicle.main_soft_fuel;
        if (vehicle.launch_soft > p->soft_spark)
            p->soft_spark = vehicle.launch_soft;
    }
}
u8 launch_permitted(u32 now) {
    const u8 *c = cal_active();
    u16 count, lock;
    if (!vehicle.armed)
        return 0;
    lock = hal_lock();
    count = ecu.vss_count;
    hal_unlock(lock);
    if (!ecu.key_input || ecu.service || !ecu.cal.valid || !(c[0x5D4] & CFG_LAUNCH) ||
        vehicle.generation != ecu.cal.generation ||
        now - vehicle.armed_at >= get16(c + CAL_LAUNCH_MS) ||
        ecu.sensors.tps.quality != QUALITY_VALID ||
        ecu.authority.inhibits ||
        (vehicle.stationary ? count != vehicle.count_at_arm :
         (!ecu.sensors.vss_valid || ecu.sensors.speed_kph >= c[0x7AF]))) {
        launch_disarm();
        vehicle.reason = 1;
        return 0;
    }
    return 1;
}

void gear_update(const Rotation *r) {
    const u8 *c = cal_active();
    u32 wheel, candidate, error, best = 0xFFFFFFFFUL, second = 0xFFFFFFFFUL;
    u8 i, selected = 0;
    vehicle.gear = 0;
    if (!ecu.sensors.vss_valid || ecu.sensors.speed_kph < 5U ||
        ecu.control.mode != ENGINE_RUNNING || r->state != ROT_VALID)
        return;
    /* Preserve legacy x100 gearbox/final-drive ratios and tyre circumference
       in mm. Report unknown outside500 RPM or when matches are ambiguous;
       this remains an estimate, not a clutch sensor or OEM gear algorithm. */
    wheel = ((u32)ecu.sensors.speed_kph * 16667UL + get16(c + 0x7AD) / 2U) /
            get16(c + 0x7AD);
    for (i = 0; i < 5U; i++) {
        candidate = (wheel * get16(c + 0x7A3U + 2U * i) + 50UL) / 100UL;
        candidate = scale32(candidate, get16(c + 0x7A1), 100U);
        error = r->rpm > candidate ? r->rpm - candidate : candidate - r->rpm;
        if (error < best) {
            second = best;
            best = error;
            selected = (u8)(i + 1U);
        } else if (error < second)
            second = error;
    }
    if (best <= 500UL && second > best + 100UL)
        vehicle.gear = selected;
}
