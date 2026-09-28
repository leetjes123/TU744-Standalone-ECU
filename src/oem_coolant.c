#include "oem.h"
#include "oem_data.h"
#define BIT_SET(word, mask, condition)                                                             \
    do {                                                                                           \
        if (condition)                                                                             \
            (word) |= (mask);                                                                      \
        else                                                                                       \
            (word) &= (65535U ^ (mask));                                                           \
    } while (0)
static u32 pair(u16 lo, u16 hi) {
    return ((u32)hi << 16) | lo;
}
static void seed(u16 *lo, u16 *hi, u16 code) {
    u32 value = (u32)(((s32)code - 64L) * 1024L);
    *lo = (u16)value;
    *hi = (u16)(value >> 16);
}
static void integrate(u16 *lo, u16 *hi, u16 *output, s16 gain, u16 maximum) {
    u32 bits = pair(*lo, *hi);
    s32 value, shifted;
    /* Two's-complement conversion without host-dependent unsigned casts. */
    value = (bits & 0x80000000UL) ? (-2147483647L - 1L) + (s32)(bits & 0x7FFFFFFFUL) : (s32)bits;
    if (gain > 0 && value > 2147483647L - gain)
        value = 2147483647L;
    else if (gain < 0 && value < (-2147483647L - 1L) - gain)
        value = (-2147483647L - 1L);
    else
        value += gain;
    bits = (u32)value;
    *lo = (u16)bits;
    *hi = (u16)(bits >> 16);
    shifted = value / 1024L;
    if (value < 0 && value % 1024L)
        shifted--; /* arithmetic floor */
    *output = (u16)clamp32(shifted + 64L, 0, maximum < 255 ? maximum : 255);
}
static u16 coordinate(const u8 *axis, u8 n, u16 x) {
    u8 i;
    if (x <= axis[0])
        return 0;
    for (i = 0; i < n - 1; i++)
        if (x < axis[i + 1])
            return (u16)(((u16)i << 8) +
                         (u16)((u32)(x - axis[i]) * 256UL / (axis[i + 1] - axis[i])));
    return (u16)((u16)(n - 1) << 8);
}
static s16 floor_lerp(s16 a, s16 b, u8 f) {
    s32 product = ((s32)b - a) * f, result = product / 256L;
    if (product < 0 && product % 256L)
        result--;
    return (s16)(a + result);
}
static s16 gain(const u8 *table, u16 x, u16 y) {
    u8 nx = table[0], ny = table[1], ix, iy, fx, fy;
    u16 cx, cy;
    const u8 *cells;
    s16 a, b;
    cx = coordinate(table + 2, nx, x);
    cy = coordinate(table + 2 + nx, ny, y);
    ix = (u8)(cx >> 8);
    iy = (u8)(cy >> 8);
    fx = (u8)cx;
    fy = (u8)cy;
    cells = table + 2 + nx + ny;
    a = (s8)cells[ix * ny + iy];
    if (fy)
        a = floor_lerp(a, (s8)cells[ix * ny + iy + 1], fy);
    if (fx) {
        b = (s8)cells[(ix + 1U) * ny + iy];
        if (fy)
            b = floor_lerp(b, (s8)cells[(ix + 1U) * ny + iy + 1], fy);
        a = floor_lerp(a, b, fx);
    }
    return a;
}
static void raw(OemCoolant *s) {
    RB03E = R95B4;
    if (R95B4 & 0x300U)
        R950C = (R95B4 >> 2) & 255U;
    else
        R950D = R95B4 & 255U;
    R9507 = coolant_raw[R95B4 & 1023U];
}
static u16 fallback(OemCoolant *s) {
    return (u16)clamp32((RB2DA & 1U) ? 91 : (R9510 > 77 ? R9510 : 77), 0, 184);
}
void oem_coolant_init(OemCoolant *s) {
    R8AFC = 200;
    R8AFE = R8B00 = 5;
    R8B02 = R8B04 = 300;
    raw(s);
    R8AFD = 0;
    R9506 = fallback(s);
    seed(&R8B10, &R8B12, R9506);
    R950E = (R9507 >= 2 && R9507 <= 253) ? R9507 : R9506;
    R8B06 = (u16)(R950E << 8);
    R950B = R9505 = R950E;
    R950A = 131;
    R9509 = R950B < R950A ? R950B : R950A;
    seed(&R8B0C, &R8B0E, R9509);
    R9BB8 = 1;
    R8AFA &= ~2U;
    if (FFD14 & 32768U)
        RAA5C = R9505 = 184;
}
void oem_coolant_reset(OemCoolant *s) {
    if (RB2E6 & 128U) {
        R9509 = R950B < R950A ? R950B : R950A;
        R8AFE = R8B00 = 5;
        R8AFA &= ~8192U;
    }
}
void oem_coolant_capture(OemCoolant *s) {
    u8 crank = (u8)((FFD16 & 2U) != 0);
    if (crank) {
        R8AFA |= 256;
        R8AFC = 200;
    } else if (R8AFC)
        R8AFC--;
    else
        R8AFA &= ~256U;
    if ((!crank && (R8AFA & 16384U)) || ((FFD16 & 4U) && (FFD6A & 64U)))
        RAA5C = R9505 = R950E;
    BIT_SET(R8AFA, 16384U, crank);
}
u16 oem_coolant_update(OemCoolant *s) {
    u16 descriptor = RB2E6;
    u8 crank = (u8)((FFD16 & 2U) != 0), running = (u8)((FFD16 & 4U) != 0),
       old_recovery = (u8)((FFD6C & 16U) != 0);
    u8 hold, expired, refresh, recovery, reset;
    u32 count, magnitude, step;
    s32 delta;
    s16 change;
    raw(s);
    BIT_SET(R8AFA, 4096U,
            (crank || running) && (R9509 > 27 ? R9509 - 27 : 0) > R9507 && !old_recovery);
    if (R8AFA & 4096U)
        descriptor = (descriptor & ~0x0F00U) | 0x0800U;
    hold = (u8)(old_recovery || crank);
    if (!hold && (R8AFA & 8U)) {
        R8B08 = R8B0A = R9BB8 = 0;
    } else if (!(FFD18 & 32U) && running && R9BB8) {
        count = pair(R8B08, R8B0A);
        R9BB8 = (u16)((count >> 8) > 65535UL ? 65535UL : count >> 8);
    }
    /* Stock 19870,19805,1981D are zero curves. Preserve retained subtype4
       latch and the held timer-pair branch, rather than clearing them. */
    expired = (u8)(R9BB8 == 0);
    BIT_SET(R8AFA, 1024U, R9507 > 253);
    if (R9507 > 253)
        descriptor = (descriptor & ~0x0F00U) | 0x0100U;
    BIT_SET(R8AFA, 2048U, R9507 < 2);
    if (R9507 < 2)
        descriptor = (descriptor & ~0x0F00U) | 0x0200U;
    if (R8AFA & 0x3C00U) {
        descriptor |= 64;
        R8AFA &= ~16U;
        R8B00 = 5;
        if (R8AFE)
            R8AFE--;
        else
            R8AFA |= 32;
    } else {
        descriptor &= ~64U;
        R8AFA &= ~32U;
        R8AFE = 5;
        if (R8B00)
            R8B00--;
        else
            R8AFA |= 16;
    }
    R9508 = (descriptor & 64U) ? R9506 : R9507;
    if (crank && !(R8AFA & 4U)) {
        R8B06 = (u16)(R9508 << 8);
        R950B = R9508;
    } else {
        delta = (s32)R9508 * 256L - R8B06;
        if (delta) {
            magnitude = (u32)(delta < 0 ? -delta : delta);
            step = (magnitude * 2570UL) >> 16;
            if (!step)
                step = 1;
            R8B06 = (u16)((s32)R8B06 + (delta < 0 ? -(s32)step : (s32)step));
        }
    }
    R950E = R8B06 >> 8;
    refresh = (u8)((R8AFA & 256U) && R9508 < 77 &&
                   (R9508 + 4U > 255U ? 255U : R9508 + 4U) <= R950B && !(descriptor & 65U));
    BIT_SET(FFD6C, 8U, refresh);
    if (R9508 > R950B || !(R8AFA & 256U))
        FFD6C &= ~16U;
    else if (refresh)
        FFD6C |= 16;
    recovery = (u8)((FFD6C & 16U) != 0);
    if (refresh)
        R950B = R950E;
    if ((R8AFA & 16U) && expired)
        descriptor = (descriptor & ~0x0F01U) | 0x2002U;
    else if (R8AFA & 32U)
        descriptor |= 0x2003U;
    R950A = 131;
    if (recovery) {
        R8AFA |= 64;
        R8B02 = 300;
    } else if (R8B02)
        R8B02--;
    else
        R8AFA &= ~64U;
    if (running) {
        if (R8B04)
            R8B04--;
        else
            R8AFA |= 2;
    } else {
        R8AFA &= ~2U;
        R8B04 = 300;
    }
    change = (!(FFD18 & 32U) && !(R8AFA & 64U) && (R8AFA & 2U)) ? gain(gain_b, RF86C, R9509) : 0;
    if ((crank && !(R8AFA & 4U)) || (!recovery && (R8AFA & 128U))) {
        R9509 = R950B < R950A ? R950B : R950A;
        seed(&R8B0C, &R8B0E, R9509);
    } else {
        if ((R9509 >= R950A && change > 0) || (!R9509 && change < 0))
            change = 0;
        integrate(&R8B0C, &R8B0E, &R9509, change, R950A);
    }
    reset = (u8)(((FFD06 & 16U) && (FFD5E & 8192U) && !R8AFD) ||
                 (crank && ((RB2DA & 1U) || (RB2EA & 1U))));
    if (FFD5E & 8192U) {
        if (R8AFD)
            R8AFD--;
    } else
        R8AFD = 0;
    if (reset && !(R8AFA & 512U)) {
        R9506 = fallback(s);
        seed(&R8B10, &R8B12, R9506);
    } else {
        change = (R8AFA & 2U) ? gain(gain_a, RF86C, R9506) : 0;
        if ((R9506 >= 184 && change > 0) || (!R9506 && change < 0))
            change = 0;
        integrate(&R8B10, &R8B12, &R9506, change, 184);
    }
    BIT_SET(R8AFA, 1U, expired);
    BIT_SET(R8AFA, 4U, crank);
    BIT_SET(R8AFA, 8U, hold);
    BIT_SET(R8AFA, 128U, recovery);
    BIT_SET(R8AFA, 512U, reset);
    return descriptor;
}
