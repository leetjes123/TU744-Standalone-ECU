#include "oem.h"

void oem_adc_publish(OemAdc *s) {
    /* Full native routine 2C188, retaining the unmasked coolant word. */
    s->input[0] = s->scan[14] & 1023U;
    s->battery = (u8)(s->scan[10] >> 2);
    s->input[1] = s->scan[9] & 1023U;
    s->input[2] = s->scan[8] & 1023U;
    s->input[3] = (u16)((s->scan[7] & 1023U) << 6);
    s->input[4] = s->scan[5];
    s->iat = (u8)(s->scan[4] >> 2);
}
u16 oem_adc_snapshot(OemAdc *s, u32 now, u16 max_age) {
    u16 lock, result, available = 0, channel_mask = 1;
    u32 stamp, observed, reference;
    u8 ch, seen;
    /* Each word, timestamp and seen flag is coherent. Exclude interrupts only
       while copying one channel, not during the loop's arithmetic. Different
       channels need not belong to one scan; this does not emulate PEC timing. */
    for (ch = 0; ch < 16; ch++) {
        lock = hal_lock();
        result = ecu.adc[ch].result;
        stamp = ecu.adc[ch].stamp;
        seen = ecu.adc[ch].seen;
        observed = ecu.milliseconds;
        hal_unlock(lock);
        reference = observed - now < 0x80000000UL ? observed : now;
        s->scan[15U - ch] = result;
        if (seen && reference - stamp <= max_age)
            available |= channel_mask;
        channel_mask = (u16)(channel_mask << 1);
    }
    oem_adc_publish(s);
    return available;
}
