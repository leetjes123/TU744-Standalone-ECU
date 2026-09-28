#ifndef LRE_IAC_HOLD_H
#define LRE_IAC_HOLD_H
#include "ecu.h"
#define HOLD_IDLE 0U
#define HOLD_WAIT 1U
#define HOLD_RX 2U
#define HOLD_NONE 256U
#define HOLD_ABORT 257U
/* T7 ticks at the inherited20MHz /16. SSCBR=128: eight SPI bits take129
   T7 ticks. These are nominal command intervals, not measured coil current. */
#define HOLD_SPI_TICKS 129U
#define HOLD_HIGH_TICKS 460U
#define HOLD_LOW_TICKS 790U
#define HOLD_LATE_TICKS 250U
#define HOLD_RX_TICKS 625U
typedef struct {
    volatile u16 due, stamp;
    volatile u8 state, enabled, high, current, pending, fault, off_needed, response, fault_response;
} IacHold;
u8 iac_hold_start(IacHold *s, u8 high, u16 now);
void iac_hold_stop(IacHold *s);
void iac_hold_fail(IacHold *s, u8 reason);
u16 iac_hold_watch(IacHold *s, u16 now, u8 bus_busy);
void iac_hold_complete(IacHold *s, u8 response, u16 now);
#endif
