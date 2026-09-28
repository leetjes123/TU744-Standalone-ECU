#ifndef LRE_OEM_RUNTIME_H
#define LRE_OEM_RUNTIME_H
#include "oem_history.h"
#include "diagnostic_monitors.h"
/* Native input obligations. A bit is set only by a complete, verified binding,
   never by a guessed flag or a tunable engineering-unit sensor conversion. */
#define OEM_INPUT_CONFIG 1U
#define OEM_INPUT_ROTATION 2U
#define OEM_INPUT_CONTEXT 4U
#define OEM_INPUT_PRODUCER_GATES 8U
#define OEM_INPUT_MIL 16U
#define OEM_INPUT_SPEED 32U
#define OEM_INPUT_ALL 63U
typedef struct {
    OemDiagnostics state;
    OemHistoryOwner history;
    OemCadence cadence;
    DiagnosticMonitors monitors;
    u32 due_ticks, last_ticks;
    u32 cycle_at, drive_since;
    s16 warmup_start_c;
    u16 bindings, adc_fresh;
    u8 booted, initialized, stopped, fault, clock_seen;
    u8 drive_timing, warmup_seeded;
    u8 drive_qualified, warmup_qualified;
    u8 drive_completed[14], warmup_completed[14];
} OemRuntime;
extern OemRuntime oem_runtime;
void oem_runtime_reset(void);
/* Called once before EINIT/interrupt enable, after the IAC has been disabled. */
void oem_runtime_boot(void);
/* tick units are the existing free-running T1 fCPU/16 units. Poll has bounded
   work; a missed whole release blocks native monitors instead of catching up
   with repeated calls on a single sample. */
void oem_runtime_poll(u32 now, u32 ticks);
void oem_runtime_stop(void);
u8 oem_runtime_save(u32 now);
u8 oem_runtime_settled(void);
/* All history mutations, including future producer bindings, must call this.
   No ISR may mutate the foreground diagnostic state. */
void oem_runtime_changed(void);
/* Physical standalone lamp owner; native steady-MIL parity stays separate. */
void diagnostic_mil_update(u32 now);
#endif
