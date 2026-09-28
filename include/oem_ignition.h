#ifndef LRE_OEM_IGNITION_H
#define LRE_OEM_IGNITION_H
#include "ecu.h"
/* Literal port of the TU5JP segment ignition scheduler sub_37CA0
   (37CA0..387B4, with sub_3886E), unchanged ROM bins/M744_C167_FULL.bin.
   Field names are the OEM RAM/SFR addresses the ROM uses; the caller maps
   the compare images onto its own channels. Counts are 1/8 tooth (0.75 deg),
   240 counts per 180-degree segment. Map: docs/audits/tu744-high-rpm-2026-09-26
   /oem/IGNITION-MAP.md. Verified against the ROM by tests/test_oem_ignition.py. */
#define OEM_IGN_IE 0x0040U
#define OEM_IGN_IR 0x0080U
#define OEM_IGN_CCM0_CC0_T1 0x0008U  /* CCM0.3  */
#define OEM_IGN_CCM1_CC6_T1 0x0800U  /* CCM1.11 */
#define OEM_IGN_CCM1_CC4_T1 0x0008U  /* CCM1.3  */
typedef struct {
    /* Words. */
    u16 fd1c, fd6a;            /* flag words FD1C, FD6A */
    u16 f8b0, f8ae;            /* segment period low word; rpm x 4 */
    u16 f7aa, f7a6;            /* segment capture (T1), T0 at that capture */
    u16 t1;                    /* FE52, used when oem_ignition_clock is null */
    u16 cap_prev, cap_last;    /* [DSTP2-4], [DSTP2-2]: last two tooth captures */
    u16 f800, f802;            /* worst observed capture age, end of pass / fire */
    u16 f7fa, f7fe, f7f6, f7f4, f7f8, f7fc;
    u16 p2, r82dc, r9716;      /* coil port image, cut masks */
    u16 ccm0, ccm1, cc0, cc4, cc6, cc0ic, cc4ic, cc6ic;
    u16 mdl, mdh;              /* multiply/divide registers across the pass */
#ifndef __C166__
    /* Record pointers the ROM stores but never reads back in this routine:
       compared by the ROM-differential test, omitted on the target so the
       state fits on-chip RAM as the ROM's F7xx variables do. */
    u16 p971e, p9718, p9726, p9722, p9724, p9720, pf7f2;
#endif
    u16 r971a[2];              /* [971A+2i]: dwell remainder (charge correction) */
    u16 dwell[2];              /* [9734+2i]: dwell time in T1 ticks */
    /* Bytes. */
    u8 adv[4];                 /* [9278+i]: signed advance in counts */
    u8 pos[4];                 /* F7E4..F7E7 */
    u8 pipe[4];                /* F7E8..F7EB */
    u8 f8d1, f8d2, f829, f8ad, r9500, r9294;
    u8 f7ec, f7dd, f7de, f7e1, f7df, f7e2, f7e0, f7f1, f7f0;
    u8 mask[4], index[4];      /* calibration 4DF4..4DF7, 4DF8..4DFB */
} OemIgnition;
/* Live T1 source for the target; null in tests (then state->t1 is used). */
extern u16 (*oem_ignition_clock)(void);
/* Called where the ROM sets IR with IE on CC0 (0), CC6 (6) or CC4 (4): its
   pass runs at low priority, so that handler is serviced at this point.
   Also called with OEM_IGN_CHARGES_OFF where the ROM disables both charge
   compares (38036, 37D7A): charges armed by the previous pass stay live until
   then, and the caller refreshes p2 (the ROM reads P2 next).
   Null in the ROM-differential test. */
#define OEM_IGN_CHARGES_OFF 0xFFU
extern void (*oem_ignition_now)(OemIgnition *s, u8 compare);
void oem_ignition_segment(OemIgnition *s);
#endif
