#include "oem_ignition.h"
/* Literal port of sub_37CA0 (and sub_3886E). Every branch follows the ROM
   listing; comments give the ROM address of each block. MULU/DIVU/DIVLU keep
   the MDL/MDH side effects the ROM relies on, including division by zero
   (MD unchanged) and DIVLU overflow (truncated quotient, V set). */
#ifdef __C166__
#define TRACE(x)
#else
#define TRACE(x) ((void)(x))
#endif
u16 (*oem_ignition_clock)(void);
void (*oem_ignition_now)(OemIgnition *s, u8 compare);

static void now(OemIgnition *s, u8 compare) { if (oem_ignition_now) oem_ignition_now(s, compare); }
static u16 t1(const OemIgnition *s) { return oem_ignition_clock ? oem_ignition_clock() : s->t1; }
static u8 mulu(OemIgnition *s, u16 a, u16 b) {
    u32 p = (u32)a * b;
    s->mdl = (u16)p; s->mdh = (u16)(p >> 16);
    return (u8)(p > 0xFFFFUL);
}
static void divu(OemIgnition *s, u16 d) {
    if (!d) return;
    s->mdh = (u16)(s->mdl % d); s->mdl = (u16)(s->mdl / d);
}
static u8 divlu(OemIgnition *s, u16 d) {
    u32 n = ((u32)s->mdh << 16) | s->mdl, q;
    if (!d) return 1;
    q = n / d;
    s->mdh = (u16)(n % d); s->mdl = (u16)q;
    return (u8)(q > 0xFFFFUL);
}
/* ROR #3 / SHR #5 split of a count into whole teeth (low byte) and substep. */
static void split(u16 counts, u8 *teeth, u8 *frac) {
    *teeth = (u8)(counts >> 3);
    *frac = (u8)(counts & 7U);
}
/* The direct T1 delay: (teeth*8 + fraction) * F7FA, saturated at FFFF. */
static u16 delay(OemIgnition *s, u8 teeth, u8 frac) {
    u16 r = (u16)(((u16)teeth << 3) + frac);
    return mulu(s, r, s->f7fa) ? 0xFFFFU : s->mdl;
}
/* Gap segment coarse clamp (FD6A.3): 26 < t < 30 -> 26 plus substeps; t >= 30
   skips the two missing teeth. Returns the coarse tooth count. */
static u16 gap_clamp(const OemIgnition *s, u16 teeth, u8 *t_byte, u8 *f_byte) {
    if (!(s->fd6a & 0x0008U) || teeth <= 26U) return teeth;
    if (teeth >= 30U) { teeth = (u16)(teeth - 2U); *t_byte = (u8)teeth; return teeth; }
    *f_byte = (u8)(*f_byte + (u8)((teeth - 26U) << 3));
    *t_byte = 26U;
    return 26U;
}
static void pipeline(OemIgnition *s) {                         /* 38788 */
    s->pipe[0] = s->pipe[1]; s->pipe[1] = s->pipe[2]; s->pipe[2] = s->pipe[3];
}
static void finish(OemIgnition *s) {                           /* 387A0 */
    u16 age = (u16)(t1(s) - s->f7aa);
    if (age > s->f800) s->f800 = age;
}
static void charge6_coarse(OemIgnition *s, u16 teeth) {        /* 38600 */
    teeth = gap_clamp(s, teeth, &s->f7e1, &s->f7df);
    s->cc6 = (u16)(s->f7a6 + teeth);
    s->ccm1 &= ~OEM_IGN_CCM1_CC6_T1;
    s->cc6ic = (u16)((s->cc6ic & ~OEM_IGN_IR) | OEM_IGN_IE);
}
static void charge6_now(OemIgnition *s) {
    s->ccm1 |= OEM_IGN_CCM1_CC6_T1;
    s->cc6ic |= OEM_IGN_IR | OEM_IGN_IE;
    now(s, 6);
}
/* Direct T1 charge on CC6; returns 1 when installed ahead, 0 when immediate. */
static u8 charge6_direct(OemIgnition *s, u8 teeth, u8 frac) {
    u16 d = delay(s, teeth, frac);
    if ((u16)(12U + t1(s) - s->f7aa) >= d) { charge6_now(s); return 0; }
    s->ccm1 |= OEM_IGN_CCM1_CC6_T1;
    s->cc6 = (u16)(s->f7aa + d);
    s->cc6ic = (u16)((s->cc6ic & ~OEM_IGN_IR) | OEM_IGN_IE);
    return 1;
}
/* Split a charge start, apply the charge-path borrow (substep <= 2) and
   select coarse (1) or direct (0). */
static u8 charge_stage(OemIgnition *s, u16 start, u16 limit, u8 *teeth, u8 *frac, u8 borrow_at_two) {
    if (start > limit) start = limit;
    split(start, teeth, frac);
    if (!*teeth) return 0;
    if (borrow_at_two ? *frac <= 2U : *frac < 2U) { *frac = (u8)(*frac + 8U); (*teeth)--; }
    return (u8)(*teeth >= s->f7dd);
}
static u8 cut(const OemIgnition *s, u16 mask) {
    return (u8)((mask & s->r82dc) || (mask & s->r9716));
}
static void second_charge(OemIgnition *s, u16 r3, u16 r6, u16 r8) { /* 3864E */
    u16 mask, r1, d;
    s->cc4ic &= ~OEM_IGN_IE;
    s->fd1c &= ~0x0100U; s->fd1c |= 0x0200U;
    mask = s->mask[r3 + 1U];
    if (cut(s, mask)) { finish(s); return; }
    r3 = s->index[r3 + 1U];
    s->f7f4 = r3;
    r1 = (u16)(r3 >= 2U ? r3 - 2U : r3);
    TRACE(s->p9724 = (u16)(0x9728U + 2U * r1)); TRACE(s->p9720 = (u16)(0x9734U + 2U * r1));
    s->mdl = s->dwell[r1]; divu(s, s->f7fa); s->f7f8 = s->mdl;
    r1 = (u16)(r6 + r8 - s->f7f8);
    if ((u16)(r6 + r8) < s->f7f8) {                            /* 3873A */
        s->ccm1 |= OEM_IGN_CCM1_CC4_T1; s->cc4ic |= OEM_IGN_IR | OEM_IGN_IE;
        now(s, 4);
        pipeline(s); finish(s); return;
    }
    if (charge_stage(s, r1, 480U, &s->f7e2, &s->f7e0, 0)) {    /* 38742 */
        u16 teeth = gap_clamp(s, s->f7e2, &s->f7e2, &s->f7e0);
        s->cc4 = (u16)(s->f7a6 + teeth);
        s->ccm1 &= ~OEM_IGN_CCM1_CC4_T1;
        s->cc4ic = (u16)((s->cc4ic & ~OEM_IGN_IR) | OEM_IGN_IE);
    } else {                                                   /* 386F0 */
        d = delay(s, s->f7e2, s->f7e0);
        s->ccm1 |= OEM_IGN_CCM1_CC4_T1;
        if ((u16)(12U + t1(s) - s->f7aa) >= d) { s->cc4ic |= OEM_IGN_IR | OEM_IGN_IE; now(s, 4); }
        else {
            s->cc4 = (u16)(s->f7aa + d);
            s->cc4ic = (u16)((s->cc4ic & ~OEM_IGN_IR) | OEM_IGN_IE);
        }
    }
    pipeline(s); finish(s);
}
static void after_charge6(OemIgnition *s, u16 r3, u16 r6, u16 r8) { /* 38646 */
    if (s->fd1c & 0x0040U) second_charge(s, r3, r6, r8);
    else { pipeline(s); finish(s); }
}
static void slot1(OemIgnition *s) {                            /* 381DC */
    u16 r3, r1, mask, start;
    s->f7de = 1;
    s->fd1c &= ~0x0100U; s->fd1c |= 0x0200U;
    r3 = s->f7ec;
    mask = s->mask[r3 + 1U];
    if (cut(s, mask)) { finish(s); return; }
    r3 = s->index[r3 + 1U];
    s->f7f6 = r3;
    s->fd1c &= ~0x0040U;
    r1 = (u16)(r3 >= 2U ? r3 - 2U : r3);
    TRACE(s->p9726 = (u16)(0x9728U + 2U * r1)); TRACE(s->p9722 = (u16)(0x9734U + 2U * r1));
    TRACE(s->pf7f2 = (u16)(0x971AU + 2U * r1));
    s->mdl = s->dwell[r1]; divu(s, s->f7fa);
    s->f7f8 = s->mdl; s->r971a[r1] = s->mdh;
    s->mdl = s->dwell[r1 + 1U < 2U ? r1 + 1U : 0U]; divu(s, s->f7fa); /* R11, unused */
    s->pipe[1] = s->pos[1];                                    /* 38298 */
    if ((u16)(s->pos[1] + 240U) < s->f7f8) {                   /* 38332 */
        u16 r7 = s->pos[1], m = (u16)(s->f7f8 - (u16)(s->pos[1] + 240U));
        u32 sum = (u32)r7 + m;
        r7 = (u16)sum;
        if (sum > 0xFFFFUL || r7 > 192U) r7 = 192U;
        s->pipe[1] = (u8)r7;
        charge6_now(s);
        pipeline(s); finish(s); return;
    }
    start = (u16)(s->pos[1] + 240U - s->f7f8);
    if (charge_stage(s, start, 480U, &s->f7e1, &s->f7df, 1)) {
        charge6_coarse(s, s->f7e1); after_charge6(s, s->f7ec, 0, 0); return;
    }
    if (charge6_direct(s, s->f7e1, s->f7df)) after_charge6(s, s->f7ec, 0, 0);
    else { pipeline(s); finish(s); }
}
static void running(OemIgnition *s) {                          /* 37F4E */
    u16 r3, r1, r6 = 0, r8 = 0, r9, mask, start, d, pos, age;
    u8 teeth, coarse = 0;
    s->fd1c &= ~0x0400U;
    pos = (s->fd1c & 0x0100U) ? s->pos[0] : s->pipe[0];
    split(pos, &s->f7f1, &s->f7f0);
    teeth = s->f7f1;
    if (teeth) {                                               /* 37F6E */
        if (s->f7f0 < 2U) { s->f7f0 = (u8)(s->f7f0 + 8U); s->f7f1 = --teeth; }
        coarse = (u8)(teeth >= s->f7dd);
    }
    if (coarse) {                                              /* 37FDE */
        u16 t = gap_clamp(s, teeth, &s->f7f1, &s->f7f0);
        s->cc0 = (u16)(s->f7a6 + t);
        s->ccm0 &= ~OEM_IGN_CCM0_CC0_T1;
        s->cc0ic = (u16)((s->cc0ic & ~OEM_IGN_IR) | OEM_IGN_IE);
    } else {                                                   /* 37F8A */
        d = delay(s, teeth, s->f7f0);
        if (d < s->f802) d = s->f802;
        s->ccm0 |= OEM_IGN_CCM0_CC0_T1;
        if ((u16)(12U + t1(s) - s->f7aa) >= d) { s->cc0ic |= OEM_IGN_IR | OEM_IGN_IE; now(s, 0); }
        else {
            s->cc0 = (u16)(s->f7aa + d);
            s->cc0ic = (u16)((s->cc0ic & ~OEM_IGN_IR) | OEM_IGN_IE);
        }
    }
    age = (u16)(t1(s) - s->f7aa);                              /* 38024 */
    if (age > s->f802) s->f802 = age;
    s->cc6ic &= ~OEM_IGN_IE; s->cc4ic &= ~OEM_IGN_IE; /* 38036 */
    now(s, OEM_IGN_CHARGES_OFF);
    r3 = s->f7ec;
    if (!(s->mask[r3] & s->p2)) {
        if (s->mask[r3 + 1U] & s->p2) slot1(s);
        else { pipeline(s); finish(s); }                       /* 3834C / 384C0 */
        return;
    }
    s->f7de = 0; s->fd1c |= 0x0100U;                           /* 38064 */
    mask = s->mask[r3];
    if (cut(s, mask)) { finish(s); return; }
    s->f7f6 = r3;
    s->fd1c &= ~0x0040U;
    TRACE(s->p9726 = (u16)(0x9728U + 2U * r3)); TRACE(s->p9722 = (u16)(0x9734U + 2U * r3));
    TRACE(s->pf7f2 = (u16)(0x971AU + 2U * r3));
    r9 = s->dwell[r3];
    s->mdl = r9; divu(s, s->f7fa);
    s->f7f8 = s->mdl; s->r971a[r3] = s->mdh;
    s->mdl = (u16)(s->dwell[r3 + 1U < 2U ? r3 + 1U : 0U] + s->f800);
    divu(s, s->f7fa);
    r6 = s->pos[1];
    if (r6 < s->mdl) { s->pipe[1] = (u8)r6; r8 = 240U; s->fd1c |= 0x0040U; }
    r1 = s->pos[0];                                            /* 3810A */
    if (s->fd1c & 0x0200U) { s->fd1c &= ~0x0200U; r1 = s->pipe[0]; }
    if (r1 < s->f7f8) {                                        /* 381A4 */
        u16 fire = r9;
        s->cc0ic &= ~OEM_IGN_IE;
        s->fd1c |= 0x2000U;
        if (s->f7f8 > 192U) {
            mulu(s, 192U, s->f8b0); divlu(s, 240U);
            fire = (u16)(s->mdl - (u16)(t1(s) - s->f7aa));
        }
        s->f7fc = fire;
        charge6_now(s);
        slot1(s);
        return;
    }
    start = (u16)(r1 - s->f7f8);
    if (charge_stage(s, start, 480U, &s->f7e1, &s->f7df, 1)) {
        charge6_coarse(s, s->f7e1); after_charge6(s, r3, r6, r8); return;
    }
    if (charge6_direct(s, s->f7e1, s->f7df)) after_charge6(s, r3, r6, r8);
    else slot1(s);                                             /* 3819A -> 381DC */
}
static void stalled(OemIgnition *s) {                          /* 37D6E */
    u16 r3, mask, r1, start;
    s->fd1c &= ~0x0040U; s->f7de = 0;
    s->fd1c |= 0x0100U; s->fd1c &= ~0x0200U;
    s->cc6ic &= ~OEM_IGN_IE; s->cc4ic &= ~OEM_IGN_IE;
    now(s, OEM_IGN_CHARGES_OFF);
    r3 = s->f7ec;
    mask = s->mask[r3];
    s->fd1c |= 0x0400U;
    if (!(s->p2 & mask)) {                                     /* 37ECA */
        u8 v;
        if (!s->f8ad) {
            mulu(s, s->f8b0, s->pipe[0]); divlu(s, 240U); v = 0;
        } else {
            mulu(s, (u16)(((u16)s->f8ad << 8) | (s->f8b0 >> 8)), (u16)((u16)s->pipe[0] << 8));
            v = divlu(s, 240U);
        }
        if (v) {                                               /* 37EF6 */
            s->mdl = 0xFFFFU;
            s->ccm0 |= OEM_IGN_CCM0_CC0_T1;
            s->cc0 = (u16)(s->f7aa + s->mdl);
            s->cc0ic = (u16)((s->cc0ic & ~OEM_IGN_IR) | OEM_IGN_IE);
            finish(s); return;
        }
        if (s->mdl < s->f800) s->mdl = s->f800;                /* 37F0C */
        s->ccm0 |= OEM_IGN_CCM0_CC0_T1;
        if ((u16)(12U + t1(s) - s->f7aa) >= s->mdl) { s->cc0ic |= OEM_IGN_IR | OEM_IGN_IE; now(s, 0); }
        else {
            s->cc0 = (u16)(s->f7aa + s->mdl);
            s->cc0ic = (u16)((s->cc0ic & ~OEM_IGN_IR) | OEM_IGN_IE);
        }
        finish(s); return;
    }
    if (cut(s, mask)) { finish(s); return; }
    s->f7f6 = r3;
    TRACE(s->p9726 = (u16)(0x9728U + 2U * r3)); TRACE(s->p9722 = (u16)(0x9734U + 2U * r3));
    TRACE(s->pf7f2 = (u16)(0x971AU + 2U * r3));
    s->r971a[r3] = 0;
    s->mdl = s->dwell[r3]; divu(s, s->f7fa);
    s->f7f8 = s->mdl; s->f7fc = s->dwell[r3];
    r1 = s->pos[0];
    if (r1 < s->f7f8) {                                        /* 37E76 */
        if (s->f7f8 > 192U) {
            if (!s->f8ad) { mulu(s, s->f8b0, 192U); divlu(s, 240U); s->f7fc = s->mdl; }
            else {
                mulu(s, (u16)(((u16)s->f8ad << 8) | (s->f8b0 >> 8)), 0xC000U);
                s->f7fc = divlu(s, 240U) ? 0xFFFFU : s->mdl;
            }
        }
        s->ccm1 |= OEM_IGN_CCM1_CC6_T1;                        /* 37EBA */
        s->cc6 = t1(s);
        s->cc6ic |= OEM_IGN_IR | OEM_IGN_IE;
        now(s, 6);
        finish(s); return;
    }
    start = (u16)(r1 - s->f7f8);
    if (charge_stage(s, start, 240U, &s->f7e1, &s->f7df, 0)) {
        charge6_coarse(s, s->f7e1); after_charge6(s, r3, 0, 0); return;
    }
    {                                                          /* 37E34 */
        u16 d = delay(s, s->f7e1, s->f7df);
        if ((u16)(12U + t1(s) - s->f7aa) >= d) {               /* 37E78 */
            if (s->f7f8 > 192U) {
                if (!s->f8ad) { mulu(s, s->f8b0, 192U); divlu(s, 240U); s->f7fc = s->mdl; }
                else {
                    mulu(s, (u16)(((u16)s->f8ad << 8) | (s->f8b0 >> 8)), 0xC000U);
                    s->f7fc = divlu(s, 240U) ? 0xFFFFU : s->mdl;
                }
            }
            s->ccm1 |= OEM_IGN_CCM1_CC6_T1;
            s->cc6 = t1(s);
            s->cc6ic |= OEM_IGN_IR | OEM_IGN_IE;
            now(s, 6);
            finish(s); return;
        }
        s->ccm1 |= OEM_IGN_CCM1_CC6_T1;
        s->cc6 = (u16)(s->f7aa + d);
        s->cc6ic = (u16)((s->cc6ic & ~OEM_IGN_IR) | OEM_IGN_IE);
        pipeline(s); finish(s);
    }
}
void oem_ignition_segment(OemIgnition *s) {
    u8 i, seg;
    for (i = 0; i < 4U; i++)                                   /* 3886E */
        s->pos[i] = (u8)(0x90 - (s16)(s8)s->adv[i]);
    if (s->f8d2 <= 1U) { finish(s); return; }                  /* 37CB0 */
    if (!(s->fd1c & 0x0800U)) {
        if (s->f8d1 != s->f829) { finish(s); return; }
        s->fd1c |= 0x0800U;
    }
    if (s->fd6a & 0x0020U) { finish(s); return; }
    if (s->cc0ic & OEM_IGN_IE) {                               /* 37CD4 */
        s->ccm0 |= OEM_IGN_CCM0_CC0_T1;
        s->cc0ic |= OEM_IGN_IR;
        now(s, 0);
    }
    seg = s->f8d1; s->r9294 = seg;
    if (seg >= 2U) seg = (u8)(seg - 2U);
    s->f7ec = seg; s->f7fe = seg;
    TRACE(s->p971e = (u16)(0x9734U + 2U * seg)); TRACE(s->p9718 = (u16)(0x972EU + 2U * seg));
    if (s->r9500 >= 3U) {
        s->mdh = s->f8ad; s->mdl = s->f8b0;
        s->f7fa = divlu(s, 240U) ? 0xFFFFU : s->mdl;
    } else
        s->f7fa = (u16)((u16)(s->cap_last - s->cap_prev) >> 3);
    s->mdl = (u16)(s->f800 + 250U);                            /* 37D48 */
    divu(s, (u16)(s->f7fa << 3));
    s->f7dd = (u8)(s->mdl + 1U);
    if (s->f8ae >= 300U) running(s);
    else stalled(s);
}
