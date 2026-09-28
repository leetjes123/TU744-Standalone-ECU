#ifndef LRE_DIAGNOSTIC_MONITORS_H
#define LRE_DIAGNOSTIC_MONITORS_H
#include "ecu.h"
#include "oem.h"

/* Backward-compatible schema-4 extension. Zero disables every standalone
   producer; one bit per OEM event gives independent activation authority. */
#define CAL_DTC_ENABLE 0x940U
#define CAL_DTC_ENABLE_BYTES 14U
#define CAL_DTC_FAIL_COUNT 0x94EU
#define CAL_DTC_PASS_COUNT 0x94FU
#define CAL_DTC_TRIM_MS 0x950U
#define CAL_DTC_O2_ACTIVITY_MS 0x952U
#define CAL_DTC_O2_SLOW_MS 0x954U
#define CAL_DTC_MISFIRE_PERCENT 0x956U
#define CAL_DTC_MISFIRE_COUNT 0x957U
#define CAL_DTC_PHASE_TIMEOUT_MS 0x958U
#define CAL_DTC_OUTPUT_FAIL_COUNT 0x95AU
#define CAL_DTC_VSS_MAX_KPH 0x95BU
#define CAL_DTC_PHASE_MIN_TICKS 0x95CU
#define CAL_DTC_PHASE_MAX_TICKS 0x95EU
#define CAL_DTC_PHASE_DELTA_TICKS 0x960U
#define CAL_DTC_PHASE_POLARITY 0x962U
#define CAL_DTC_TPS_SLEW 0x964U /* tenths-percent per second */
#define CAL_DTC_MAP_SLEW 0x966U /* kPa per second */
#define CAL_DTC_TEMP_SLEW 0x968U /* degrees C per second */
#define CAL_DTC_BATTERY_SLEW 0x96AU /* mV per second */
#define CAL_DTC_SUBTYPE_ENABLE 0x980U /* 107 low-nibble masks: 1/2/4/8. */

#define MONITOR_UNKNOWN 0U
#define MONITOR_PASS 1U
#define MONITOR_FAIL 2U
#define MISFIRE_WINDOW_SAMPLES 128U

typedef struct {
    u8 fail[107], pass[107], enabled[107];
    u16 previous_rotation_epoch, previous_speed, phase_epoch, misfire_epoch;
    u32 previous_losses, vss_moving_at;
    u16 o2_crossings, o2_last_mv, phase_last_delay;
    s16 previous_tps, previous_map, previous_iat, previous_clt, previous_battery;
    u32 trim_limit_since, o2_window_at, o2_last_cross_at, phase_last_at;
    u32 last_update;
    u32 misfire_baseline[2];
    u8 completed[CAL_DTC_ENABLE_BYTES];
    u8 misfire_slot[2], misfire_phase[2], misfire_hits[10];
    u8 misfire_seeded, misfire_samples, misfire_qualified, misfire_unattributed;
    u8 rotation_seeded, crank_was_valid, crank_pending, vss_moving, vss_lost, phase_seeded;
    u8 initialized, o2_side, phase_valid, phase_cylinder_one, processor_test,
       sensor_seeded;
} DiagnosticMonitors;

typedef struct {
    volatile u32 armed_at, armed_ms, captured_at, delay;
    volatile u16 rotation_epoch;
    volatile u8 armed, captured, missed, tooth;
} PhaseObservation;

#define MISFIRE_QUEUE_SIZE 8U
typedef struct {
    volatile u32 duration[MISFIRE_QUEUE_SIZE];
    volatile u16 epoch[MISFIRE_QUEUE_SIZE];
    volatile u32 at_ms[MISFIRE_QUEUE_SIZE];
    volatile u8 slot[MISFIRE_QUEUE_SIZE], phase[MISFIRE_QUEUE_SIZE], eligible[MISFIRE_QUEUE_SIZE];
    volatile u8 head, tail, overflow, seeded;
    volatile u32 boundary;
    volatile u16 phase_epoch;
    volatile u8 phase_identity, boundary_eligible;
} MisfireObservation;

extern PhaseObservation phase_observation;
extern MisfireObservation misfire_observation;
void diagnostic_phase_arm(u32 stamp, u16 epoch, u8 tooth) SHARED;
void diagnostic_phase_capture(u32 stamp) SHARED;
void diagnostic_rotation_edge(u32 stamp, u16 epoch, u8 tooth, u8 gap,
                              u8 valid) SHARED;
u8 diagnostic_edge_eligible(void) SHARED;
void diagnostic_rotation_ineligible(void) SHARED;

void diagnostic_monitors_init(DiagnosticMonitors *m);
u8 diagnostic_monitor_enabled(const u8 *cal, u8 event);
u8 diagnostic_monitor_subtype_enabled(const u8 *cal, u8 event, u8 subtype);
void diagnostic_monitors_update(DiagnosticMonitors *m, OemDtcState *events,
                                u32 now, const u8 *cal);

#endif
