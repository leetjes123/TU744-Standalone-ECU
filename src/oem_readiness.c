#include "oem.h"

/*6C116 constructs the supported mask from native configuration, rearms the
   per-cycle counters, and replaces pending ONLY on the lost-history branch.
   It does not reset retained counters even when FD14.15 is set. */
void oem_readiness_init(OemReadiness *s) {
    s->supported = (u8)((s->fd02 >> 12) & 3U);
    if (s->config_a & 0x6000U)
        s->supported |= 4U;
    if (s->config_b & 1U)
        s->supported |= 8U;
    if (s->config_a & 8U)
        s->supported |= 32U;
    if (s->config_a & 64U)
        s->supported |= 64U;
    s->once = 255U;
    if (s->startup_flags & 0x8000U)
        s->pending = s->supported;
}

static void group(OemReadiness *s, u8 mask, u8 index, u16 complete, u16 failed) {
    if (!(s->pending & mask) || !(complete & 2U))
        return;
    if (s->count[index] < 255U && (s->once & mask)) {
        s->count[index]++;
        s->once &= (u8)~mask;
    }
    if (!(failed & 1U) || s->count[index] >= oem_readiness_limits[index])
        s->pending &= (u8)~mask;
}

/*6BEDA..6C114. For grouped descriptors ALL bit1s must be set; ANY
   bit0 selects the fault/count threshold. The once bit is consumed only
   when a counter actually increments, including its254->255 transition. */
void oem_readiness_update(OemReadiness *s) {
    u16 *d = s->descriptor;
    if (!s->pending)
        return;
    group(s, 1U, 1U, d[0], d[0]);
    s->pending &= 0xFDU;
    group(s, 4U, 4U, (u16)(d[1] & d[2]), (u16)(d[1] | d[2]));
    group(s, 8U, 3U, d[3], d[3]);
    s->pending &= 0xEFU;
    group(s, 32U, 2U, (u16)(d[4] & d[5] & d[6] & d[7] & d[8]),
          (u16)(d[4] | d[5] | d[6] | d[7] | d[8]));
    group(s, 64U, 0U, (u16)(d[9] & d[10]), (u16)(d[9] | d[10]));
    s->pending &= 0x7FU;
}

/*6C18E runs after the record clear worker, for every accepted clear kind. */
void oem_readiness_clear(OemReadiness *s) {
    u8 i;
    for (i = 0; i < 5U; i++)
        s->count[i] = 0;
    s->pending = s->supported;
    s->once = 255U;
}
