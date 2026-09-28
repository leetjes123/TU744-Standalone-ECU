#ifndef LRE_OEM_H
#define LRE_OEM_H
#include "ecu.h"
/* Native descending-channel PEC buffer and 2C188 publication. The standalone
   ADC frontend retains the original ADDAT word alongside its tunable inputs. */
typedef struct {
    u16 scan[16];    /* F7B0..F7CE: AN15..AN0 */
    u16 input[5];    /* 95B0,95B8,95B6,95BA,95B4 */
    u8 battery, iat; /* 9209,9208 */
} OemAdc;
#define OEM_ADC_COOLANT 4U
void oem_adc_publish(OemAdc *s);
/* Returns a channel freshness bitmap, independent of native fault triggers.
   max_age is an explicit standalone transport contract, not an OEM threshold. */
u16 oem_adc_snapshot(OemAdc *s, u32 now, u16 max_age);
/* Native-ROM input contracts. These are not engineering-unit sensors.
   Integration must supply each native state/gate and its original cadence. */
typedef struct {
    u8 state, retained, prove_count, prove_flags, flash_count, demand, lamp;
    u16 fd08, fd0e, fd12, fd5a, fd6a;
} OemMil;
void oem_mil_update(OemMil *s);
void oem_mil_on(OemMil *s);
void oem_mil_off(OemMil *s);
void oem_mil_clear(OemMil *s);
typedef struct {
    u16 value[33];
    u16 flags[7];
} OemCoolant;
extern const u16 oem_coolant_addresses[33];
extern const u16 oem_flag_addresses[7];
u16 oem_coolant_update(OemCoolant *s);
void oem_coolant_init(OemCoolant *s);
void oem_coolant_capture(OemCoolant *s);
void oem_coolant_reset(OemCoolant *s);

typedef struct {
    u16 descriptor, coolant_descriptor, status, filter, startup_flags, run_flags;
    u8 adc, raw, filtered, captured, pass_count, fail_count, coolant;
} OemIat;
void oem_iat_init(OemIat *s);
void oem_iat_reset(OemIat *s);
u16 oem_iat_update(OemIat *s);
void oem_iat_capture(OemIat *s);

typedef struct {
    u16 run_flags, rotation_flags, count_a, running_count;
    u8 coolant, iat, speed;
} OemEngineState;
void oem_engine_init(OemEngineState *s);
void oem_engine_update(OemEngineState *s);

/* Native input words, in the address order documented by the generated
   oem-input-layout.json. Outputs cover the contiguous bytes951B..9528. */
typedef struct {
    u16 value[14];
    u8 coolant, vehicle_speed, output[14];
} OemContext;
void oem_context_update(OemContext *s);

typedef struct {
    u16 descriptor, speed_descriptor, status, run_flags;
    u16 fraction, filter_high, scaled, filtered;
    u8 adc, voltage, scaled_byte, filtered_byte;
    u8 delay, fail_count, pass_count, divider, vehicle_speed;
} OemVoltage;
void oem_voltage_init(OemVoltage *s);
void oem_voltage_reset(OemVoltage *s);
void oem_voltage_base(OemVoltage *s);
void oem_voltage_filter(OemVoltage *s);
void oem_voltage_alternate(OemVoltage *s);
u16 oem_voltage_update(OemVoltage *s);

/* Native event68 producer inputs, not standalone km/h or inferred network
   health. Source selection and operating gates retain their ROM identities. */
typedef struct {
    u16 descriptor, source_a_descriptor, source_b_descriptor, status;
    u16 fd06, fd08, fd18, fd52, fd5e, speed, condition_speed;
    u8 source, source_a_status, source_b_status;
    u8 fail_count, pass_count, source_count, coolant, engine_speed, load;
} OemVss;
void oem_vss_reset(OemVss *s);
u16 oem_vss_update(OemVss *s);

/* Native speed acquisition state and requested SFR values. These routines
   never touch board registers. The owner must serialize the capture snapshot. */
typedef struct {
    u16 descriptor, fd00, fd06, fd08, status;
    u16 speed, physical_speed, source_speed, distance, target;
    u16 batch, previous_speed, capture, previous_capture, period;
    u16 numerator_low, numerator_high, fraction, filter_high;
    u16 source_fraction, source_filter, source_target, pulse_total;
    u16 source_a, source_b, timer, pecc5, ccm3, srcp5, dstp5, cc14ic;
    u8 source, acceleration_status, acceleration_input, stale_count;
    u8 next_batch, active_batch, captured_batch, source_count;
    u8 vehicle_speed, acceleration;
} OemVssInput;
void oem_vss_input_init(OemVssInput *s);
void oem_vss_input_capture(OemVssInput *s);
void oem_vss_input_update(OemVssInput *s);

/* Seven raw digital pins and their native publication state. Port/direction
   and clock words are data; this API never applies native DP2 to the board. */
typedef struct {
    u16 published, older, newer, sample;
    u16 p4, p5, p6, p8, fd04, fd08, fd0a, fd2c, fd2e, dp2;
    u16 clock_low, clock_high, clock_irq, stamp_low, stamp_high, stamp_flags;
    u8 pulse_count, pulse_state, retained_byte, source_byte;
} OemDigital;
void oem_digital_init(OemDigital *s);
void oem_digital_filter(OemDigital *s);
void oem_digital_publish(OemDigital *s);
void oem_digital_aux(OemDigital *s);

/* Native divider counts, not milliseconds. The owner supplies the previously
   published F8AC byte and serializes delivery of the returned release mask.
   Bits 0..4 correspond to divider2/5/10/20/100 in native order. */
typedef struct {
    u16 period;
    u8 countdown[5], rejected[5], adaptation, speed, enabled;
} OemCadence;
void oem_cadence_init(OemCadence *s);
u8 oem_cadence_step(OemCadence *s);
void oem_cadence_rejected(OemCadence *s, u8 mask);

/* Byte-exact native record storage. Words inside the 24-byte records are
   little-endian, unlike the tuning transport. Native phase addresses are tags,
   not pointers into standalone RAM. See docs/OEM-DIAGNOSTICS.md. */
#define OEM_DTC_SLOTS 20U
#define OEM_DTC_RECORD_SIZE 24U
typedef struct {
    u8 records[OEM_DTC_SLOTS][OEM_DTC_RECORD_SIZE];
    u8 count, demand, active_demand;
    u8 phases[7];
} OemDtcRecords;
extern const u8 oem_dtc_configs[38][14];
extern const u8 oem_dtc_event_configs[107];
extern const u8 oem_dtc_event_counts[107];
extern const u16 oem_dtc_phase_addresses[7];
extern const u16 oem_dtc_drive_delay;
extern const u8 oem_dtc_warmup_limits[3];
u8 oem_dtc_remove(OemDtcRecords *s, u8 slot);
u8 oem_dtc_phase(OemDtcRecords *s, u16 phase_address);
/* Standalone adapter: only events evaluated in this release may progress. */
u8 oem_dtc_phase_masked(OemDtcRecords *s, u16 phase_address, const u8 *completed);
u8 oem_dtc_aggregate(OemDtcRecords *s);
typedef struct {
    OemDtcRecords store;
    u16 live[107];
    u16 gate, timestamp;
    u8 last_event, overflow;
    u8 context[9];
    u16 clear_request, clear_inverse, clear_mode;
    u16 startup_flags;
    u8 scan_event, clear_previous;
    u8 scan_record, scan_unused, clear_wait, lock_wait;
    u16 run_flags, clock_divider, drive_timer, drive_count;
    u8 coolant, warmup_start, warmup_count;
} OemDtcState;
u8 oem_dtc_assert(OemDtcState *s, u8 event, u16 descriptor);
u8 oem_dtc_recover(OemDtcState *s, u8 event, u16 descriptor);
u8 oem_dtc_complete(OemDtcState *s, u8 event);
u8 oem_dtc_subtype(OemDtcState *s, u8 event, u16 descriptor);
u8 oem_dtc_ingest(OemDtcState *s, u8 event, u16 *descriptor);
u8 oem_dtc_age(OemDtcState *s);
u8 oem_dtc_clear_worker(OemDtcState *s);
/* Standalone admission guard for composing reset callbacks before the worker.
   The low-level native worker intentionally has weaker request validation. */
u8 oem_dtc_clear_ready(const OemDtcState *s);
u8 oem_dtc_cycle_begin(OemDtcState *s);
/* On return 1 the owner must schedule the deferred clear worker. */
u8 oem_dtc_clear_all(OemDtcState *s);
u8 oem_dtc_clear_emissions(OemDtcState *s);
u8 oem_dtc_clear_event(OemDtcState *s, u8 event);
u8 oem_dtc_maintain(OemDtcState *s);
u8 oem_dtc_drive_init(OemDtcState *s);
u8 oem_dtc_drive_update(OemDtcState *s);
u8 oem_dtc_drive_clear(OemDtcState *s);
u8 oem_dtc_warmup_init(OemDtcState *s);
u8 oem_dtc_warmup_update(OemDtcState *s);
u8 oem_dtc_warmup_clear(OemDtcState *s);
void oem_dtc_clock(OemDtcState *s);

/* Native monitor-completion state,6BEDA/6C116/6C18E. Descriptor inputs
   retain their native event identities; no absent monitor is marked complete. */
typedef struct {
    u16 descriptor[11]; /* events2F,5F,5D,53,45,44,3E,40,3F,2D,2B */
    u16 fd02, config_a, config_b, startup_flags; /* FD02,959C,959E,FD14 */
    u8 count[5];        /* retained AA68..AA6C, calibration11949..1194D */
    u8 supported, pending, once; /*952C, retainedAA6D,8B3E */
} OemReadiness;
extern const u8 oem_readiness_limits[5];
void oem_readiness_init(OemReadiness *s);
void oem_readiness_update(OemReadiness *s);
void oem_readiness_clear(OemReadiness *s);

/* Shared native diagnostic state. Canonical shared flags live in events
   (FD14/FD6C) and engine (FD16/FD6A). Other producer flags remain explicit
   inputs; no missing equipment or operating gate is synthesized as passing. */
typedef struct {
    OemDtcState events;
    OemCoolant coolant;
    OemIat iat;
    OemEngineState engine;
    OemContext context;
    OemMil mil;
    OemVoltage voltage;
    OemVss vss;
    OemVssInput vss_input;
    OemDigital digital;
    OemReadiness readiness;
} OemDiagnostics;
void oem_diagnostics_adc(OemDiagnostics *s, u16 coolant, u8 iat, u8 voltage);
void oem_diagnostics_bind(OemDiagnostics *s);
void oem_diagnostics_voltage_base(OemDiagnostics *s);
u8 oem_diagnostics_voltage(OemDiagnostics *s);
u8 oem_diagnostics_vss(OemDiagnostics *s);
void oem_diagnostics_vss_input(OemDiagnostics *s);
void oem_diagnostics_digital(OemDiagnostics *s);
void oem_diagnostics_digital_aux(OemDiagnostics *s);
u8 oem_diagnostics_sensors(OemDiagnostics *s);
void oem_diagnostics_capture(OemDiagnostics *s);
void oem_diagnostics_context(OemDiagnostics *s);
void oem_diagnostics_engine(OemDiagnostics *s);
u8 oem_diagnostics_demand(OemDiagnostics *s);
void oem_diagnostics_readiness(OemDiagnostics *s);
/* Only the currently ported callbacks from native task29620, in native order.
   Not the complete OEM clear task or a diagnostic-tool acknowledgement. */
u8 oem_diagnostics_clear_ported(OemDiagnostics *s);
#endif
