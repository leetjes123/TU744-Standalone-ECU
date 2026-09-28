#include "ecu.h"
u16 get16(const u8 *p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}
void put16(u8 *p, u16 v) {
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}
u32 get32(const u8 *p) {
    return ((u32)get16(p) << 16) | get16(p + 2);
}
void put32(u8 *p, u32 v) {
    put16(p, (u16)(v >> 16));
    put16(p + 2, (u16)v);
}
s32 clamp32(s32 v, s32 lo, s32 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
u32 scale32(u32 v, u16 m, u16 d) {
    if (!d)
        return 0xFFFFFFFFUL;
    if (m && v > 0xFFFFFFFFUL / m)
        return 0xFFFFFFFFUL;
    return v * m / d;
}
u16 us_ticks(u16 us) SHARED {
    /* ceil(5*us/4) = us + floor(us/4) + (us%4 != 0).
       Preserve the u16 result, including wrap, without long arithmetic in
       the output interrupt path. No intermediate overflows before the sum. */
    return (u16)(us + (us >> 2) + ((us & 3U) != 0U));
}
u16 crc16(u16 crc, const u8 *p, u16 n) {
    u8 i;
    while (n--) {
        crc ^= (u16)*p++ << 8;
        for (i = 0; i < 8; i++)
            crc = (u16)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
    }
    return crc;
}
/* a + (b - a) * f / 256 with C truncation toward zero, as the original signed
   long division, but with one 16x16 multiply and a shift: |b - a| < 65536. */
s16 lerp(s16 a, s16 b, u16 f) {
    s32 d = (s32)b - a;
    if (d >= 0)
        return (s16)((s32)a + (s32)(((u32)(u16)d * f) >> 8));
    return (s16)((s32)a - (s32)(((u32)(u16)(-d) * f) >> 8));
}
#define AXIS_WORD(p) ((u16)(((u16)(p)[0] << 8) | (p)[1]))
/* Each axis point is read once and compared in its own 16-bit domain (a
   negative x precedes every unsigned point). Where x lies between two points
   the span b - a and offset x - a are below 65536, so the interpolation is
   the original (x - a) * 256 / (b - a), exactly. */
u16 axis_fraction(const u8 *axis, u8 n, s16 x, u8 signed_axis, u8 *index) {
    u8 i;
    u16 a, b, offset = 0, span = 0;
    a = AXIS_WORD(axis);
    if (signed_axis ? x <= (s16)a : (x < 0 || (u16)x <= a)) {
        *index = 0;
        return 0;
    }
    for (i = 0; i < n - 1; i++) {
        b = AXIS_WORD(axis + 2U * (i + 1U));
        if (signed_axis ? x < (s16)b : (u16)x < b) {
            *index = i;
            if (signed_axis ? (s16)b > (s16)a : b > a) {
                offset = (u16)((u16)x - a);
                span = (u16)(b - a);
                return (u16)(((u32)offset << 8) / span);
            }
            return 0;
        }
        a = b;
    }
    *index = (u8)(n - 2);
    return 256;
}
void axis_at(const u8 *axis, u8 n, s16 x, u8 signed_axis, AxisAt *at) {
    at->fraction = axis_fraction(axis, n, x, signed_axis, &at->index);
}
u16 table1_at(const u8 *c, u16 offset, const AxisAt *t, u8 wide) {
    u8 i = t->index;
    s16 a, b;
    a = wide ? (s16)(((u16)c[offset + 2U * i] << 8) | c[offset + 2U * i + 1U]) : c[offset + i];
    b = wide ? (s16)(((u16)c[offset + 2U * i + 2U] << 8) | c[offset + 2U * i + 3U]) : c[offset + i + 1U];
    return (u16)lerp(a, b, t->fraction);
}
u8 table2_at(const u8 *c, u16 offset, const AxisAt *rpm, const AxisAt *load) {
    const u8 *row = c + offset + 16U * load->index + rpm->index;
    s16 a, b;
    a = lerp(row[0], row[1], rpm->fraction);
    b = lerp(row[16], row[17], rpm->fraction);
    return (u8)lerp(a, b, load->fraction);
}
u16 table1(const u8 *c, u16 offset, s16 t, u8 wide) {
    AxisAt at;
    axis_at(c + 0x460, 16, t, 1, &at);
    return table1_at(c, offset, &at, wide);
}
u8 table2(const u8 *c, u16 offset, u16 rpm, u16 load, u16 load_axis) {
    AxisAt x, y;
    axis_at(c + 0x400, 16, (s16)rpm, 0, &x);
    axis_at(c + load_axis, 16, (s16)load, 0, &y);
    return table2_at(c, offset, &x, &y);
}
u16 voltage_table(const u8 *c, u16 offset, u16 mv) {
    /* Preserve the legacy 4,6,8,10,12,14,16,18 V axis. */
    u16 i, f;
    if (mv <= 4000)
        return get16(c + offset);
    if (mv >= 18000)
        return get16(c + offset + 14);
    i = (mv - 4000U) / 2000U;
    f = (u16)((u32)(mv - 4000U - i * 2000U) * 256UL / 2000UL);
    return (u16)lerp((s16)get16(c + offset + 2U * i), (s16)get16(c + offset + 2U * i + 2U), f);
}
