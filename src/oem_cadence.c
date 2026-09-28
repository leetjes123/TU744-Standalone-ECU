#include "oem.h"

/* TU5JP 28F3A..28F60. Other fields intentionally retain their caller's state:
   this initialization region does not reset the adaptation/error counters. */
void oem_cadence_init(OemCadence *s) {
    s->countdown[0] = 1;
    s->countdown[1] = 2;
    s->countdown[2] = 4;
    s->countdown[3] = 10;
    s->countdown[4] = 20;
    s->period = 781;
}

/* TU5JP 28D9A..28EB6, with RTOS delivery as an explicit owner boundary.
   The original tests the DECREMENTED byte as signed; zero wraps to 255
   and expires too. Use unsigned tests so host char signedness is irrelevant.
   Period units are T4 ticks (12.8 us only at 20 MHz / 256). */
u8 oem_cadence_step(OemCadence *s) {
    static const u8 reload[5] = {2, 5, 10, 20, 100};
    u8 i, due = 0, count;
    u16 period;
    for (i = 0; i < 5; i++) {
        count = (u8)(s->countdown[i] - 1U);
        if (!count || count >= 128U) {
            count = reload[i];
            due |= (u8)(s->enabled & (1U << i));
        }
        s->countdown[i] = count;
    }
    s->adaptation = (u8)(s->adaptation - 1U);
    if (!s->adaptation || s->adaptation >= 128U) {
        s->adaptation = 5;
        period = 781;
        if (s->speed > 175U)
            period += (u16)((u16)(s->speed - 175U) * 4U);
        s->period = period > 976U ? 976U : period;
    }
    return due;
}

/* Apply only to releases the owner attempted and the queue rejected. Native
   counters wrap; disabled descriptors never attempt release or count failure.
   This function does not claim OEM task preemption/queue semantics. */
void oem_cadence_rejected(OemCadence *s, u8 mask) {
    u8 i;
    for (i = 0; i < 5; i++)
        if (mask & (1U << i))
            s->rejected[i]++;
}
