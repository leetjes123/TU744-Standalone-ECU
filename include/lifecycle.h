#ifndef LRE_LIFECYCLE_H
#define LRE_LIFECYCLE_H
#include "ecu.h"

/* Schema3 extension in previously unused bytes. Zero preserves the old,
   inhibited analog-only policy. All changes require a stopped engine. */
#define CAL_WB_POLICY 0x926U
#define CAL_WB_WARM_MS 0x928U
#define CAL_WB_GOOD_MS 0x92AU
#define CAL_WB_MIN_MV 0x92CU
#define CAL_WB_MAX_MV 0x92EU
#define CAL_LAUNCH_MS 0x930U
#define CAL_LAUNCH_SOFT_RPM 0x932U
#define CAL_LAUNCH_SOFT_PERCENT 0x934U
#define CAL_ANTILAG_MS 0x936U
#define CAL_ANTILAG_CLT 0x938U
#define CAL_ANTILAG_IAT 0x939U
#define CAL_DWELL_FEEDBACK 0x93AU /* 0=fixed calibrated dwell, 1=qualified CC9 */
#define WB_DISABLED 0U
#define WB_WARMING 1U
#define WB_QUALIFYING 2U
#define WB_ANALOG_QUALIFIED 3U
#define WB_UNAVAILABLE 4U
#define WB_DIRECT 5U /* policy 0: used whenever its sample is valid, no warm-up */
typedef struct {
    u32 started, good_since, last;
    u16 generation;
    u8 state, warming, qualifying;
} WidebandState;
extern WidebandState wideband;
void wideband_update(u32 now);

typedef struct {
    u32 armed_at, antilag_at;
    u16 generation, count_at_arm;
    u8 armed, stationary, reason, gear, antilag, antilag_used, antilag_started;
    u8 main_soft_fuel, launch_soft;
} VehicleState;
extern VehicleState vehicle;
/* Stationary mode is an explicit operator assertion, never inferred from a
   silent VSS wire. The arm expires and is consumed by any subsequent motion. */
u8 launch_arm(u8 stationary, u32 now);
void launch_disarm(void);
u8 launch_permitted(u32 now);
void gear_update(const Rotation *r);
void antilag_update(u32 now, const Rotation *r, EnginePlan *p);

#define POWER_BOOT 0U
#define POWER_RUN 1U
#define POWER_DRAIN 2U
#define POWER_SAVE 3U
#define POWER_HELD 4U
#define POWER_RELEASED 5U
#define POWER_RESTART_REQUIRED 6U
#define POWER_RESET_REQUESTED 7U
typedef struct {
    u32 sample_at, off_at;
    u8 state, history, samples, sampled, save_attempted, release_blocked, observation_valid;
} PowerState;
extern PowerState power;
void lifecycle_init(void);
void power_poll(u32 now);
/* Raw OEM run-permission observation (P4.4), not a connector pin identity.
   Return2 for an unavailable observation. Power release must fail closed until
   the board's actual latch circuit has been verified. */
u8 hal_run_permission(void);
u8 hal_power_release(void);
u32 hal_capture_clock(void);
void hal_system_reset(void);
/* Written by START167 after C initialization, before main. Kept outside Ecu
   and PowerState so their initialization cannot erase the boot observation.
   Only WDTR has a stepping-independent interpretation; see LIFECYCLE.md. */
#define RESET_CAPTURE_VALID 0x5253U
#define RESET_KNOWN_MASK 0x0002U
extern u16 reset_capture_raw, reset_capture_marker;
#endif
