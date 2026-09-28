#include "lifecycle.h"
WidebandState wideband;

void wideband_update(u32 now) {
    const u8 *c = cal_active();
    u16 raw, lock;
    u32 mv;
    u8 valid;
    Sensors *s = &ecu.sensors;
    s->wideband_ready = 0;
    /* This board has no ready wire. No path consults wideband_input. */
    if (!c[0x600] || !(c[CAL_FLAGS] & EQUIP_UPSTREAM_RELAY_HEATER) || !ecu.key_input ||
        !ecu.cal.valid || !ecu.board_released || ecu.service) {
        wideband.state = WB_DISABLED;
        wideband.warming = wideband.qualifying = 0;
        return;
    }
    if (!c[CAL_WB_POLICY]) {
        /* No qualification: the controller output is trusted as soon as the
           sample is valid (sensor quality covers range and staleness). */
        wideband.warming = wideband.qualifying = 0;
        wideband.state = s->oxygen.quality == QUALITY_VALID ? WB_DIRECT : WB_UNAVAILABLE;
        s->wideband_ready = (u8)(wideband.state == WB_DIRECT);
        return;
    }
    /* Warm-up restarts at key-on, on a signal dropout and when the policy
       becomes active, not on every calibration write. */
    if (!wideband.warming) {
        wideband.started = now;
        wideband.generation = ecu.cal.generation;
        wideband.warming = 1;
        wideband.qualifying = 0;
    }
    lock = hal_lock();
    raw = ecu.adc[6].raw;
    hal_unlock(lock);
    mv = (u32)raw * 5000UL / 1023UL;
    valid = (u8)(s->oxygen.quality == QUALITY_VALID &&
                  mv >= get16(c + CAL_WB_MIN_MV) && mv <= get16(c + CAL_WB_MAX_MV));
    /* A missing service interval breaks continuous qualification as well as
       stale/rail input. Filtered voltage cannot conceal a raw rail fault. */
    if (!valid || (wideband.qualifying && now - wideband.last > get16(c + CAL_SENSOR_AGE))) {
        wideband.qualifying = 0;
        /* With no controller-ready feedback, a dropout may be a restart.
           Require the full configured warm-up again, not merely good_ms. */
        wideband.started = now;
        wideband.state = WB_UNAVAILABLE;
        wideband.last = now;
        return;
    }
    wideband.last = now;
    if (now - wideband.started < get16(c + CAL_WB_WARM_MS)) {
        wideband.state = WB_WARMING;
        return;
    }
    if (!wideband.qualifying) {
        wideband.good_since = now;
        wideband.qualifying = 1;
    }
    wideband.state = WB_QUALIFYING;
    if (now - wideband.good_since >= get16(c + CAL_WB_GOOD_MS)) {
        wideband.state = WB_ANALOG_QUALIFIED;
        s->wideband_ready = 1;
    }
}
