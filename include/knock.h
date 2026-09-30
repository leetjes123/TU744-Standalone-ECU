#ifndef TU744_KNOCK_H
#define TU744_KNOCK_H
#include "ecu.h"
/* Schema 5. All words are big endian; timing/retard counts are 0.75 degrees. */
#define CAL_KNOCK 0xA00U
#define CAL_KNOCK_MODE 0xA00U
#define CAL_KNOCK_FILTER 0xA01U
#define CAL_KNOCK_MIN_RPM 0xA02U
#define CAL_KNOCK_COOLANT 0xA04U
#define CAL_KNOCK_DIVISOR 0xA05U
#define CAL_KNOCK_GAIN 0xA06U
#define CAL_KNOCK_REFERENCE 0xA07U
#define CAL_KNOCK_LATCH_MS 0xA08U
#define CAL_KNOCK_STALE_MS 0xA0AU
#define CAL_KNOCK_DRIFT_LIMIT 0xA0CU
#define CAL_KNOCK_NULL_TOLERANCE 0xA0DU
#define CAL_KNOCK_TEST_SHIFT 0xA0EU
#define CAL_KNOCK_DEBOUNCE 0xA0FU
#define CAL_KNOCK_MANUAL_GAIN 0xA10U
#define CAL_KNOCK_ATTACK_STEP 0xA11U
#define CAL_KNOCK_RETARD_LIMIT 0xA12U
#define CAL_KNOCK_RECOVERY_PERCENT 0xA13U
#define CAL_KNOCK_RPM_AXIS 0xA20U
#define CAL_KNOCK_START 0xA40U
#define CAL_KNOCK_LENGTH 0xA50U
#define CAL_KNOCK_THRESHOLD 0xA60U
#define CAL_KNOCK_LOAD 0xA70U
#define CAL_KNOCK_ATTACK 0xA80U
#define CAL_KNOCK_MAXIMUM 0xA90U
#define CAL_KNOCK_HOLD 0xAA0U
#define CAL_KNOCK_GAIN_CODES 0xAB0U
#define KNOCK_DISABLED 0U
#define KNOCK_MONITOR 1U
#define KNOCK_CONTROL 2U
#define KNOCK_NORMAL 0U
#define KNOCK_NULL 1U
#define KNOCK_TEST 2U
#define KNOCK_GAIN_TEST 3U
#define KNOCK_FAULT_ADC 1U
#define KNOCK_FAULT_STALE 2U
#define KNOCK_FAULT_WINDOW 3U
#define KNOCK_FAULT_REFERENCE 4U
#define KNOCK_FAULT_NULL 5U
#define KNOCK_FAULT_TEST 6U
#define KNOCK_FAULT_RAIL 7U
#define KNOCK_FAULT_DRIFT 8U
typedef struct {
    u16 generation, epoch, latch_ms, stale_ms, hold;
    u8 mode, eligible, start, length, threshold, attack, maximum;
    u8 divisor, initial_gain, initial_reference, gain_code[7], manual_gain;
    u8 drift_limit, null_tolerance, test_shift, debounce;
} KnockConfig;
typedef struct {
    u16 raw, mv, sequence, epoch, generation, hold, window_ticks;
    u32 stamp, last_knock, count, normal_count, null_count, test_count;
    u16 adc_timeouts, missed_windows, stale_samples, ceiling_events;
    s16 drift_q16;
    u8 mode, fault, valid, source, type, amplitude, offset, null_start;
    u8 reference, gain, gain_code, ratio, threshold, decision, recent;
    u8 retard, scheduled, qualified, good_count, rail_count;
    u8 bench, null_bad, test_bad, last_normal, drift_bad;
} KnockState;
extern KnockState knock;
extern KnockConfig knock_config;
typedef struct {
    u16 null_raw, test_raw, gain_raw[7];
    u8 state, completed, fault;
} KnockJob;
extern KnockJob knock_job_result;
void knock_init(void);
void knock_defaults(u8 *c);
u8 knock_validate(const u8 *c, u16 *error);
void knock_update(u32 now, const Rotation *r, const u8 *c);
void knock_invalidate(u8 fault);
void knock_sample(const KnockConfig *cfg, u16 raw, u8 type, u16 ticks, u32 now);
void knock_null_start(u16 raw);
void knock_monitor(u8 *out, u32 now);
void knock_details(u8 *out, u32 now);
u8 knock_job(u8 command, u32 now);
void knock_job_status(u8 *out, u32 now);
s16 knock_advance(s16 base);
u16 knock_mv(u16 raw);
/* Register-free detector, for unchanged-ROM differential tests. */
void knock_detect(KnockState *s, const KnockConfig *cfg, u8 raw);
/* Hardware owner; native fixtures supply these interfaces. */
void hal_knock_cancel(void);
void hal_knock_configure(u8 filter);
void hal_knock_bench(u8 job, u32 now);
#endif
