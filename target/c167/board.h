#ifndef LRE_BOARD_H
#define LRE_BOARD_H
#include "ecu.h"
#include "reg167.h"
/* Default development profile inhibits engine outputs. The explicitly named
   engine-experimental profile overrides this for ECU testing; that override
   is output permission, not a claim that hardware acceptance is complete. */
#ifndef BOARD_RELEASED
#define BOARD_RELEASED 0
#endif
#define IRQ(level, group) (0x40U | ((level) << 2) | (group))
#define IRQ_HANDLER(vector) interrupt vector
/* Bit-addressed writes must not restore neighbouring actuator latch bits. */
sbit PIN_COIL_A = P2 ^ 0;
sbit PIN_COIL_B = P2 ^ 1;
sbit PIN_TACH = P2 ^ 13;
sbit PIN_INJ_1 = P7 ^ 6;
/* OEM order (Keil, unchanged ROM): P7.6, P7.5, P7.4, P8.7 fire 180 deg apart, so with
   firing order 1-3-4-2 the TDC companions are P7.6/P7.4 and P7.5/P8.7. */
sbit PIN_INJ_4 = P7 ^ 4;
sbit PIN_INJ_3 = P7 ^ 5;
sbit PIN_INJ_2 = P8 ^ 7;
sbit PIN_PUMP = P8 ^ 1;
sbit PIN_BOOST = P8 ^ 3;
sbit PIN_IAC_SELECT = P8 ^ 2;
sbit PIN_EEPROM_SELECT = P4 ^ 7;
sbit PIN_FAN_A = P7 ^ 1;
sbit PIN_FAN_B = P3 ^ 15;
sbit PIN_GAUGE = P7 ^ 2;
sbit PIN_HEATER_UP = P6 ^ 2;
sbit PIN_HEATER_DOWN = P6 ^ 4;
sbit PIN_MIL = P3 ^ 4;
/* Knock IC (Bosch CC195), engines/TU5JP/archive/36-knock-ic-cc195-init-and-calibration.md */
sbit PIN_KNOCK_G0 = P3 ^ 1;   /* gain select G0 */
sbit PIN_KNOCK_G1 = P3 ^ 2;   /* gain select G1 */
sbit PIN_KNOCK_G2 = P3 ^ 3;   /* gain select G2 */
sbit PIN_KNOCK_KTI = P3 ^ 5;  /* test pulse enable */
sbit PIN_KNOCK_KSA3 = P3 ^ 6; /* 1 = sensor inputs disconnected */
sbit PIN_KNOCK_MF = P8 ^ 0;   /* measurement window, 1 = integrate */
sbit PIN_KNOCK_BF2 = P8 ^ 5;  /* band-pass select bit 2 */
/* OEM file 0x18BF0: nominal band-pass centre frequency in kHz. */
#define KNOCK_FILTER_KHZ_DEFAULT 16U
/* OEM knock-IC boot timeline, measured by running the unchanged ROM in the
   Keil C166 simulator (own BUSCON0 wait states, stock 95080 contents),
   20 MHz CPU states from reset (tests/keil_knock_boot.py):
     1 090 687  P3 latch   1 090 693  DP3 (P3 drivers on)
     1 090 762  P8 latch   1 090 768  DP8 (MF low)
     1 411 604  G1 low     1 411 610  KTI low     1 411 613  KSA3 low
   The standalone reproduces it with T1 (fCPU/16, running from reset): port
   init when T1, after its first overflow at 52.43 ms, reaches
   KNOCK_IC_PORT_T1; KNOCK_IC_P3_P8_NOPS loop passes between the
   P3 and P8 writes, and the release KNOCK_IC_HOLD_T1 after MF low. Values are
   trimmed against the Keil trace; Keil does not charge BUSCON1 wait states,
   so confirm on a stock ECU by scope. */
#define KNOCK_IC_PORT_T1 2619U
#define KNOCK_IC_P3_P8_NOPS 3U
#define KNOCK_IC_HOLD_T1 20049U
void board_init(void);
void board_knock_ic_boot(u8 filter_khz);
void board_knock_ic_release(u8 filter_khz);
u16 hal_hard_lock(void);
void hal_hard_unlock(u16 old);
void board_schedule(u32 captured);
void board_ignition_segment(u8 tooth, u32 stamp, u16 counter, u16 interval);
void board_capture_init(void);
void board_outputs_init(void);
u16 board_recent_period(u16 *captured);
u8 board_capture_snapshot(u16 *counter, u16 *captured, u16 *period);
extern volatile u16 capture_schedule_counter;
extern volatile u16 capture_overruns, capture_blocks;
void board_ssc_tick(void);
extern volatile u16 capture_high;
extern volatile u8 coil_phase[2];
extern volatile u16 coil_event_epoch[2];
#endif
