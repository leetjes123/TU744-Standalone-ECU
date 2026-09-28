#include "ecu.h"
#include "lifecycle.h"
#include "faults.h"
void adc_publish(u16 result, u32 now) SHARED {
    u8 ch = (u8)(result >> 12);
    ecu.adc[ch].result = result;
    ecu.adc[ch].raw = result & 1023U;
    ecu.adc[ch].stamp = now;
    ecu.adc[ch].generation++;
    ecu.adc[ch].seen = 1;
}
static u16 sample(u8 ch, u8 alpha, u32 now, u8 *quality) {
    u16 lock, raw, generation;
    u32 stamp, observed;
    u8 seen;
    Sensors *s = &ecu.sensors;
    lock = hal_lock();
    raw = ecu.adc[ch].raw;
    stamp = ecu.adc[ch].stamp;
    generation = ecu.adc[ch].generation;
    seen = ecu.adc[ch].seen;
    observed = ecu.milliseconds;
    hal_unlock(lock);
    /* The ISR may publish after the control pass captured its release time.
       Advance the reference to the clock sampled with this channel, including
       wrap, so a new conversion cannot look almost 2^32 milliseconds old. */
    if (observed - now < 0x80000000UL)
        now = observed;
    *quality = (!seen || now - stamp > get16(cal_active() + CAL_SENSOR_AGE)) ? QUALITY_STALE
                                                                             : QUALITY_VALID;
    if (raw <= 1 || raw >= 1022)
        *quality = QUALITY_RANGE;
    if (ch == 5U && ((u32)raw * 1000UL / 36UL < get16(cal_active() + CAL_MIN_BAT) ||
                    (u32)raw * 1000UL / 36UL > 18000UL))
        *quality = QUALITY_RANGE;
    /* A bounded 10 ms control release performs at most one filter update.
       Age is independent of the generation counter's wrap. */
    if (*quality != QUALITY_VALID) {
        /* Faulted inputs use the latest captured raw value, not a last-good
           value or a substitute. With stale acquisition this is necessarily
           the last captured reading. Preserve the failed quality separately. */
        return raw;
    }
    if (!s->seeded[ch]) {
        s->filtered[ch] = raw;
        s->seeded[ch] = seen;
    } else if (generation != s->generation[ch])
        s->filtered[ch] = (u16)(((u32)s->filtered[ch] * alpha + (u32)raw * (256U - alpha)) / 256UL);
    s->generation[ch] = generation;
    return s->filtered[ch];
}
static s16 ntc(u16 raw, const u8 *c, u16 at) {
    u32 resistance;
    u16 a, b, f;
    u8 i;
    resistance = (u32)get16(c + at) * raw / (1024U - raw);
    if (resistance >= get16(c + at + 2))
        return (s16)get16(c + 0x460);
    for (i = 0; i < 15; i++) {
        a = get16(c + at + 2 + 2U * i);
        b = get16(c + at + 4 + 2U * i);
        if (resistance >= b) {
            f = (u16)(((u32)a - resistance) * 256UL / (a - b));
            return lerp((s16)get16(c + 0x460 + 2U * i), (s16)get16(c + 0x462 + 2U * i), f);
        }
    }
    return (s16)get16(c + 0x47E);
}
void sensors_update(u32 now) {
    const u8 *c = cal_active();
    Sensors *s = &ecu.sensors;
    u16 raw, a, b, f;
    u8 i;
    raw = sample(8, c[0x628], now, &s->tps.quality);
    a = get16(c + 0x7B0);
    b = get16(c + 0x7B2);
    if (b <= a) {
        s->tps.quality = QUALITY_CONFIG;
        s->tps.value = 0;
    } else
        s->tps.value =
            (s16)(clamp32((s32)raw - a, 0, b - a) * 1000L / (b - a)); /* tenths percent */
    raw = sample(0, c[0x629], now, &s->map.quality);
    a = (u16)((u32)raw * 15UL);
    i = (u8)(a / 1023U);
    f = (u16)((u32)(a % 1023U) * 256UL / 1023UL);
    s->map.value = i >= 15
                       ? (s16)get16(c + 0x5B2)
                       : lerp((s16)get16(c + 0x594 + 2U * i), (s16)get16(c + 0x596 + 2U * i), f);
    raw = sample(10, c[0x62A], now, &s->clt.quality);
    s->clt.value = ntc(raw, c, 0x550);
    raw = sample(11, c[0x62B], now, &s->iat.quality);
    s->iat.value = ntc(raw, c, 0x572);
    raw = sample(5, c[0x62D], now, &s->battery.quality);
    s->battery.value = (s16)((u32)raw * 1000UL / 36UL);
    if (s->battery.value < get16(c + CAL_MIN_BAT) || s->battery.value > 18000)
        s->battery.quality = QUALITY_RANGE;
    raw = sample(6, c[0x62C], now, &s->oxygen.quality);
    s->oxygen_mv = (u16)((u32)raw * 5000UL / 1023UL);
    s->afr10 = (u16)(c[0x601] + (u32)raw * (c[0x602] - c[0x601]) / 1023UL);
    s->oxygen.value = (s16)s->afr10;
    sensor_faults(now);
    wideband_update(now);
    s->stamp = now;
}
