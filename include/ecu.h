#ifndef LRE_ECU_H
#define LRE_ECU_H
/* ISO C90 core. All wire and persistent integers have explicit byte order. */
#ifdef __C166__
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
#define SHARED /* C166 functions use the user stack and are reentrant by default. */
#define SYSTEM_RAM sdata /* on-chip RAM, 16-bit near address */
#else
#include <stdint.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
#define SHARED
#define SYSTEM_RAM
#endif
typedef char check_u16[(sizeof(u16) == 2) ? 1 : -1];
typedef char check_u32[(sizeof(u32) == 4) ? 1 : -1];
#include "storage_layout.h"

#define CAL_SIZE 3072U
#define CAL_SCHEMA 5U
#define CAL_MAGIC 0x900U
#define CAL_RUN_RPM 0x904U
#define CAL_CRANK_RPM 0x906U
#define CAL_RUN_MS 0x908U
#define CAL_AE_DECAY 0x90AU
#define CAL_STFT_KI 0x90CU
#define CAL_MAX_MAP 0x90EU
#define CAL_IAC_MAX 0x910U
#define CAL_STOICH 0x912U
#define CAL_VSS_PPM 0x914U
#define CAL_FLAGS 0x916U
#define CAL_INJ_PHASE 0x918U
#define CAL_CRANK_ADV 0x91AU
#define CAL_HOME_MS 0x91CU
#define CAL_MIN_BAT 0x91EU
#define CAL_SENSOR_AGE 0x920U
#define CAL_PLAN_AGE 0x922U
#define CAL_IDLE_STEP_MS 0x924U
/* IAC closing steps driven during homing; zero keeps the original 250. */
#define CAL_IAC_HOME_STEPS 0x93CU
/* Zero preserves the 200 RPM restart margin of existing schema-4 tunes. */
#define CAL_DFCO_EXIT_RPM 0x93EU
#define DFCO_EXIT_RPM_DEFAULT 200U
#define IAC_HOME_STEPS_DEFAULT 250U
#define IAC_HOME_MS_PER_STEP 12U /* 10 ms step cadence plus scheduling margin */
/* Geometry, units and sensor/equipment changes require a stopped engine. */
#define CFG_ALPHA_N 0x01U
#define CFG_STFT 0x20U
#define CFG_DFCO 0x02U
#define CFG_BOOST 0x10U
#define CFG_LAUNCH 0x04U
#define EQUIP_UPSTREAM_RELAY_HEATER 0x01U
#define EQUIP_DOWNSTREAM_RELAY_HEATER 0x02U

#define INH_SYNC 0x0001U
#define INH_CAL 0x0002U
#define INH_SENSOR 0x0004U
#define INH_STALE 0x0008U
#define INH_SERVICE 0x0010U
#define INH_DEADLINE 0x0020U
#define INH_POWER 0x0040U
#define INH_BOARD 0x0080U
#define INH_OUTPUT 0x0100U
#define AUX_SHUTDOWN_INHIBITS (INH_CAL | INH_SERVICE | INH_DEADLINE | INH_POWER | INH_BOARD | INH_OUTPUT)
#define CAL_TRANSACTION_MS 5000UL
#define CUT_REV 0x0001U
#define CUT_DFCO 0x0002U
#define CUT_FLOOD 0x0004U
#define CUT_LAUNCH 0x0008U
#define CUT_BOOST 0x0010U
#define QUALITY_VALID 1U
#define QUALITY_STALE 2U
#define QUALITY_RANGE 4U
#define QUALITY_CONFIG 8U
#define ENGINE_STOPPED 0U
#define ENGINE_CRANKING 1U
#define ENGINE_RUNNING 2U
#define ROT_UNSYNCED 0U
#define ROT_ACQUIRING 1U
#define ROT_VALID 2U
#define IAC_UNKNOWN 0U
#define IAC_HOMING 1U
#define IAC_READY 2U
#define IAC_FAULT 3U
#define IDLE_UNAVAILABLE 0U
#define IDLE_CRANK 1U
#define IDLE_CATCH 2U
#define IDLE_FEEDBACK 3U
#define IDLE_RETURN 4U
#define IDLE_OFF 5U

typedef struct {
    u16 raw, result; /* Preserve the captured ADDAT word for native diagnostics. */
    u32 stamp;
    u16 generation;
    u8 seen;
} AdcSample;
typedef struct {
    s16 value;
    u8 quality;
} Sensor;
typedef struct {
    Sensor tps, map, clt, iat, battery, oxygen;
    u16 filtered[16], generation[16];
    u8 seeded[16];
    u16 speed_kph, oxygen_mv, afr10;
    u8 vss_valid, wideband_ready;
    u32 stamp;
} Sensors;
typedef struct {
    volatile u32 last, normal, revolution, gap_stamp, losses;
    volatile u16 rpm, epoch, cycle;
    volatile u8 state, tooth, seen, have_gap;
} Rotation;
typedef struct {
    volatile u16 generation, epoch, fuel_cut, spark_cut;
    volatile u16 pulse_us, requested_us, dwell_us, rpm, injection_phase10, max_age_ms;
    volatile s16 advance10, trigger10;
    volatile u8 soft_fuel, soft_spark, dwell_feedback;
    volatile u32 stamp;
} EnginePlan;
typedef struct {
    volatile u16 inhibits, epoch;
    volatile u32 faults, first_time;
    volatile u16 first_reason;
    EnginePlan plan;
    volatile u8 injector_active[4], coil_active[2];
    volatile u16 coil_epoch[2], fuel_accumulator[2], spark_accumulator[2];
    volatile u16 feedback_missing[2], feedback_invalid[2], late_events;
    volatile s16 feedback_correction[2];
    volatile u8 spark_draining;
} Authority;
typedef struct {
    u8 bytes[2][CAL_SIZE];
    u8 active, staging, valid, dirty;
    u16 generation, error_offset;
    /* Live map-cell writes (protocol 32). They leave generation unchanged:
       generation restarts wideband warm-up, disarms launch and aborts saves. */
    u16 live_edits;
    u32 touched_at;
} Calibration;
typedef struct {
    u8 mode, rev_limited, dfco, launch, fan, pump, gauge, boost;
    u8 fan_request, fan_waiting, throttle_closed;
    u16 fan_target, idle_fan_steps;
    u32 fan_at;
    u8 warmup, afterstart, target_afr, idle_mode;
    u16 ve; /* Running or cranking VE percentage. */
    u32 qualify_at, running_at, dfco_at, prime_until, last_control, last_trim;
    u8 qualifying, dfco_waiting, key_on, previous_key, trim_enabled, ae_seeded;
    u16 ae_percent, ae_peak, previous_tps, idle_target, idle_position;
    u32 ae_at, ae_qualify_at, idle_step_at;
    u8 ae_qualifying, rich, lambda_type;
    s16 tps_rate, idle_spark10;
    s32 trim_q16, idle_integral;
    u16 applied_trim, previous_generation;
    /* Map cell of the last fuel plan: axis index and fraction (0..256). */
    u8 cell_rpm, cell_load;
    u16 cell_rpm_fraction, cell_load_fraction;
} Controls;
typedef struct {
    u8 state, phase, previous_active, open_count, fault, response, pending;
    u8 off_pending, holding;
    u16 position, target, remaining;
    u32 deadline, next_step, fault_count;
} Iac;
typedef struct {
    volatile u8 ring[128], head, tail, overflow;
    u8 packet[40], used, length, checksum, state;
    u32 started;
    u8 tx[132];
    u16 tx_length, tx_position;
    u32 rejected, dropped;
    u8 job, step; /* stepped calibration transaction in progress */
    u8 update;    /* command 01 accepted: enter the update handler after the reply */
    u32 update_at;
} Protocol;
typedef struct {
    u8 phase, slot, active_slot, valid, header[32], verify[32];
    u16 offset, crc, generation;
    u32 sequence, deadline;
    u8 result;
} Storage;
typedef struct {
    u16 live[128];
    u8 support[128]; /* 0=pending, 1=producer verified, 2=full parity verified */
    u8 mil_steady, mil_flashing_requested, mil_output, mil_flash_active;
    u32 mil_flash_at;
} Diagnostics;
typedef struct {
    Calibration cal;
    volatile AdcSample adc[16];
    Sensors sensors;
    Rotation rotation;
    Authority authority;
    Controls control;
    Iac iac;
    Protocol protocol;
    Storage storage;
    const void *storage_owner; /* Foreground journal lease; reset clears it. */
    Diagnostics diagnostics;
    volatile u32 milliseconds, foreground_stamp;
    volatile u16 vss_count;
    u16 vss_previous;
    u32 vss_stamp, watchdog_stamp;
    volatile u32 vss_edge_stamp;
    u8 board_released;
    volatile u8 service, key_input, wideband_input;
} Ecu;
extern Ecu ecu;
/* Worst observed timing since boot or the last protocol 2F reset. Bench
   evidence for CPU headroom on the actual ECU (simulation cannot establish
   external bus timing). u16 fields saturate at 65535. */
typedef struct {
    volatile u16 pass_max_ms, interval_max_ms, tick_max_ms, plan_age_max_ms;
    volatile u16 capture_overruns;
    /* Counter/capture mismatches recovered by resynchronization. */
    volatile u16 capture_resyncs;
    /* First capture failure only, published by level 15 with reason last.
       Read-only until reset; ordinary timing reports retain their wire format. */
    volatile u16 capture_reason, capture_counter, capture_expected;
    volatile u16 capture_last, capture_latest, capture_irq, capture_pec, capture_dest;
    volatile u16 capture_words[30]; /* the failed block, unused words 0 */
    volatile u8 capture_head, capture_tail, capture_next, capture_count;
    volatile u8 seen;
} TimingHealth;
extern TimingHealth timing_health;

/* HAL: calls are bounded; critical enter/leave preserves the prior state. On
   the C167 hal_lock raises the CPU level to 14; crank capture stays at 15.
   Engine hardware writes below are made only with interrupts masked. */
u16 hal_lock(void) SHARED;
void hal_unlock(u16 state) SHARED;
void hal_cancel_all(void) SHARED;
void hal_cancel_fuel(void) SHARED;
void hal_cancel_spark(void) SHARED;
void hal_revoke_spark(u8 lost_angle) SHARED;
u8 hal_injector_start(u8 channel, u16 ticks) SHARED;
u8 hal_coil_start(u8 channel, u16 ticks) SHARED; /* 0=fault, 1=started, 2=late/revoked skip */
void hal_coil_off(u8 channel) SHARED;
void hal_phase_arm(void) SHARED;
void hal_phase_disarm(void) SHARED;
void hal_aux(u8 pump, u8 fan, u8 boost, u8 gauge, u8 heaters, u8 mil);
u8 hal_iac_transfer(u8 command, u8 *response);
u8 hal_iac_hold_start(u8 high_command);
u8 hal_iac_hold_fault(void);
u8 hal_iac_hold_response(void);
u8 hal_eeprom_read(u16 address, u8 *bytes, u8 length);
u8 hal_eeprom_write(u16 address, const u8 *bytes, u8 length);
u8 hal_eeprom_busy(void); /* 0=ready, 1=busy, 2=bus failure */
void hal_watchdog_service(void);
void hal_uart_send(u8 byte);
/* Enter the RAM firmware-update handler; returns only if it cannot start. */
void hal_firmware_update(void);
u8 hal_uart_ready(void);

u16 get16(const u8 *p);
void put16(u8 *p, u16 value);
u32 get32(const u8 *p);
void put32(u8 *p, u32 value);
s32 clamp32(s32 value, s32 low, s32 high);
u32 scale32(u32 value, u16 multiplier, u16 divisor);
u16 us_ticks(u16 us) SHARED;
u16 crc16(u16 crc, const u8 *p, u16 count);
s16 lerp(s16 a, s16 b, u16 fraction);
u16 axis_fraction(const u8 *axis, u8 n, s16 x, u8 signed_axis, u8 *index);
typedef struct {
    u8 index;
    u16 fraction;
} AxisAt;
void axis_at(const u8 *axis, u8 n, s16 x, u8 signed_axis, AxisAt *at);
u16 table1_at(const u8 *cal, u16 offset, const AxisAt *temperature, u8 wide);
u8 table2_at(const u8 *cal, u16 offset, const AxisAt *rpm, const AxisAt *load);
u16 table1(const u8 *cal, u16 offset, s16 temperature, u8 wide);
u8 table2(const u8 *cal, u16 offset, u16 rpm, u16 load, u16 load_axis);
u16 voltage_table(const u8 *cal, u16 offset, u16 millivolts);

void cal_init(void);
u8 cal_validate(const u8 *bytes, u16 *error);
u8 cal_begin(void);
u8 cal_write(u16 offset, const u8 *bytes, u8 count);
u8 cal_commit(u8 stopped);
u8 cal_commit_check(void);
u8 cal_commit_apply(u8 stopped);
void cal_abort(void);
u8 cal_live_write(u16 offset, const u8 *bytes, u8 count);
void cal_poll(u32 now);
void cal_example(u8 *bytes); /* test/recovery template, never auto-enabled */
const u8 *cal_active(void);
void sensors_update(u32 now);
void adc_publish(u16 result, u32 now) SHARED;
/* Returns one only for a newly accepted, synchronized tooth. */
u8 rotation_edge(u32 capture_ticks) SHARED;
u8 rotation_block(volatile u16 SYSTEM_RAM *cap, u8 count, u32 last_stamp, u8 eligible, u32 *fastest) SHARED;
void rotation_snapshot(Rotation *out);
void safety_inhibit(u16 reason, u32 now) SHARED;
void safety_conditions(u16 reasons, u32 now);
u8 safety_aux_permitted(void) SHARED;
u8 safety_publish(const EnginePlan *plan) SHARED;
u8 injector_admit(u8 pair) SHARED;
u8 coil_admit(u8 channel, u16 epoch, u16 dwell_us) SHARED;
void injector_done(u8 channel) SHARED;
void coil_done(u8 channel) SHARED;
void ecu_init(u8 board_released);
void ecu_tick(void) SHARED;
void ecu_tick_elapsed(u16 elapsed_ms) SHARED;
void ecu_poll(void);
void controls_update(u32 now, const Rotation *rotation);
void iac_home(u32 now);
void iac_service(u32 now);
void iac_disable(void);
void protocol_receive(u8 byte) SHARED;
void protocol_poll(u32 now);
void protocol_service(u32 now, u8 steps);
void protocol_tx_continue(void) SHARED;
void storage_load(void);
/* Foreground/boot only. A lease spans a complete journal transaction, including
   EEPROM internal write time. It is separate from the per-transfer SSC lease. */
u8 storage_claim(const void *owner);
void storage_release(const void *owner);
u8 storage_save(u32 now);
void storage_poll(u32 now);
u8 service_enter(u32 now);
u16 dtc_report_word(u8 event, u8 subtype);
#define DTC_INGEST_NEW 1U
#define DTC_INGEST_CLEAR 2U
#define DTC_INGEST_COMPLETE 4U
#define DTC_INGEST_SUBTYPE 8U
u8 dtc_ingest_action(u16 previous, u16 descriptor);
#endif
