#include "oem.h"
#include "oem_layout.h"

void oem_diagnostics_adc(OemDiagnostics *s, u16 coolant, u8 iat, u8 voltage) {
    /* Inputs already published by the native ADC conversion contract. No
       engineering-unit sensor calibration or synthetic passing gate enters. */
    s->coolant.value[OEM_CLT_95B4] = coolant;
    s->iat.adc = iat;
    s->voltage.adc = voltage;
}

/* These bindings reproduce shared native RAM identities. The typed producer
   states do not own independent copies of live descriptors or global flags.
   The frontend must provide real native inputs before releasing these tasks. */
static void bind(OemDiagnostics *s) {
    static const u8 readiness_events[11] = {0x2F, 0x5F, 0x5D, 0x53, 0x45, 0x44,
                                           0x3E, 0x40, 0x3F, 0x2D, 0x2B};
    u8 i;
    OemCoolant *c = &s->coolant;
    c->value[OEM_CLT_B2DA] = s->events.live[0x5B];
    c->value[OEM_CLT_B2E6] = s->events.live[0x61];
    c->value[OEM_CLT_B2EA] = s->events.live[0x63];
    c->value[OEM_CLT_9510] = s->iat.filtered;
    c->flags[OEM_FLAG_FD14] = s->events.startup_flags;
    c->flags[OEM_FLAG_FD16] = s->engine.run_flags;
    c->flags[OEM_FLAG_FD6A] = s->engine.rotation_flags;
    c->flags[OEM_FLAG_FD6C] = s->events.gate;
    s->iat.descriptor = s->events.live[0x5B];
    s->iat.coolant_descriptor = s->events.live[0x61];
    s->iat.coolant = (u8)c->value[OEM_CLT_950E];
    s->iat.startup_flags = s->events.startup_flags;
    s->iat.run_flags = s->engine.run_flags;
    s->engine.coolant = (u8)c->value[OEM_CLT_950E];
    s->engine.iat = s->iat.filtered;
    s->context.coolant = (u8)c->value[OEM_CLT_9507];
    s->events.coolant = (u8)c->value[OEM_CLT_950E];
    s->events.run_flags = s->engine.run_flags;
    s->events.context[3] = s->context.output[6];
    s->events.context[4] = s->context.output[1];
    s->events.context[5] = s->context.output[0];
    s->events.context[6] = s->context.output[2];
    s->events.context[7] = s->engine.speed;
    s->events.context[8] = s->context.output[13];
    s->mil.fd6a = s->engine.rotation_flags;
    s->mil.demand = s->events.store.demand;
    s->mil.lamp = (u8)((s->events.gate >> 8) & 1U);
    s->voltage.descriptor = s->events.live[0x65];
    s->voltage.speed_descriptor = s->events.live[0x68];
    s->voltage.run_flags = s->engine.run_flags;
    s->voltage.vehicle_speed = s->context.vehicle_speed;
    s->vss.descriptor = s->events.live[0x68];
    s->vss.source_a_descriptor = s->events.live[0x18];
    s->vss.source_b_descriptor = s->events.live[0x19];
    s->vss.fd06 = c->flags[OEM_FLAG_FD06];
    s->vss.fd08 = s->mil.fd08;
    s->vss.fd18 = c->flags[OEM_FLAG_FD18];
    s->vss.fd5e = c->flags[OEM_FLAG_FD5E];
    s->vss.coolant = (u8)c->value[OEM_CLT_950E];
    s->vss.engine_speed = s->engine.speed;
    s->vss_input.descriptor = s->events.live[0x68];
    s->vss_input.fd06 = s->vss.fd06;
    s->vss_input.fd08 = s->mil.fd08;
    s->vss_input.status = s->vss.status;
    s->vss_input.speed = s->vss.speed;
    s->vss_input.physical_speed = s->vss.condition_speed;
    s->vss_input.source = s->vss.source;
    s->vss_input.source_count = s->vss.source_count;
    s->vss_input.vehicle_speed = s->context.vehicle_speed;
    s->digital.fd08 = s->mil.fd08;
    s->digital.clock_low = s->vss_input.timer;
    s->readiness.startup_flags = s->events.startup_flags;
    for (i = 0; i < 11U; i++)
        s->readiness.descriptor[i] = s->events.live[readiness_events[i]];
}

void oem_diagnostics_bind(OemDiagnostics *s) {
    bind(s);
}

/* Normal divider20 entry24, following the demand/MIL entries20..23. */
void oem_diagnostics_readiness(OemDiagnostics *s) {
    bind(s);
    oem_readiness_update(&s->readiness);
}

void oem_diagnostics_digital(OemDiagnostics *s) {
    bind(s);
    oem_digital_filter(&s->digital);
    oem_digital_publish(&s->digital);
    s->mil.fd08 = s->digital.fd08;
    bind(s);
}
void oem_diagnostics_digital_aux(OemDiagnostics *s) {
    bind(s);
    oem_digital_aux(&s->digital);
}

void oem_diagnostics_vss_input(OemDiagnostics *s) {
    bind(s);
    oem_vss_input_update(&s->vss_input);
    s->mil.fd08 = s->vss_input.fd08;
    s->vss.status = s->vss_input.status;
    s->vss.speed = s->vss_input.speed;
    s->vss.condition_speed = s->vss_input.physical_speed;
    s->context.vehicle_speed = s->vss_input.vehicle_speed;
    bind(s);
}

u8 oem_diagnostics_vss(OemDiagnostics *s) {
    u16 descriptor;
    bind(s);
    descriptor = oem_vss_update(&s->vss);
    s->coolant.flags[OEM_FLAG_FD06] = s->vss.fd06;
    s->mil.fd08 = s->vss.fd08;
    if (!oem_dtc_ingest(&s->events, 0x68, &descriptor))
        return 0;
    bind(s);
    return 1;
}

void oem_diagnostics_voltage_base(OemDiagnostics *s) {
    bind(s);
    oem_voltage_base(&s->voltage);
}
u8 oem_diagnostics_voltage(OemDiagnostics *s) {
    u16 descriptor;
    bind(s);
    descriptor = oem_voltage_update(&s->voltage);
    if (!oem_dtc_ingest(&s->events, 0x65, &descriptor))
        return 0;
    bind(s);
    return 1;
}

/* Divider10 normal entries5/6, in native order. The IAT descriptor reaches
   ingestion before coolant reads its fault bit for substitute selection.
   Each producer reads the PREVIOUS shared state and then publishes its result. */
u8 oem_diagnostics_sensors(OemDiagnostics *s) {
    u16 descriptor;
    bind(s);
    descriptor = oem_iat_update(&s->iat);
    if (!oem_dtc_ingest(&s->events, 0x5B, &descriptor))
        return 0;
    bind(s);
    descriptor = oem_coolant_update(&s->coolant);
    s->events.gate = s->coolant.flags[OEM_FLAG_FD6C];
    if (!oem_dtc_ingest(&s->events, 0x61, &descriptor))
        return 0;
    bind(s);
    return 1;
}

/* Divider100 entries6/7. No assumed time unit is attached to a call. */
void oem_diagnostics_capture(OemDiagnostics *s) {
    bind(s);
    oem_iat_capture(&s->iat);
    oem_coolant_capture(&s->coolant);
    bind(s);
}

/* The original base calls6B8BA before329D6. Keep these separate entry points
   because intervening native tasks and their inputs must retain their order. */
void oem_diagnostics_context(OemDiagnostics *s) {
    bind(s);
    s->context.coolant = (u8)s->coolant.value[OEM_CLT_9507];
    oem_context_update(&s->context);
    s->events.context[3] = s->context.output[6];  /* 9521 */
    s->events.context[4] = s->context.output[1];  /* 951C */
    s->events.context[5] = s->context.output[0];  /* 951B */
    s->events.context[6] = s->context.output[2];  /* 951D */
    s->events.context[7] = s->engine.speed;       /* F8AC */
    s->events.context[8] = s->context.output[13]; /* 9528 */
}
void oem_diagnostics_engine(OemDiagnostics *s) {
    bind(s);
    oem_engine_update(&s->engine);
    bind(s);
}

/* Divider20 normal entries20..23. Confirmation runs before lamp policy;
   aggregation still preserves its native reduction-before-confirmation order.
   Flashing demand is retained as data; no periodic flashing function is called. */
u8 oem_diagnostics_demand(OemDiagnostics *s) {
    bind(s);
    if (!oem_dtc_drive_update(&s->events) || !oem_dtc_warmup_update(&s->events) ||
        !oem_dtc_aggregate(&s->events.store))
        return 0;
    s->mil.demand = s->events.store.demand;
    oem_mil_update(&s->mil);
    s->events.gate = (u16)((s->events.gate & 0xFEFFU) | (s->mil.lamp ? 256U : 0U));
    bind(s);
    return 1;
}

u8 oem_diagnostics_clear_ported(OemDiagnostics *s) {
    /* Validate BEFORE even refreshing aliases: rejected work is unchanged.
       A single foreground owner must exclude all other diagnostic mutation. */
    if (!oem_dtc_clear_ready(&s->events))
        return 0;
    bind(s);
    /* Calls at29624/29628/29638/2965C/296A8/296AC/296B4. Unported
       callbacks from29620 are explicitly outside this projection. Preserve
       live bit7 and the request until every included reset has read them. */
    oem_dtc_drive_clear(&s->events);
    oem_dtc_warmup_clear(&s->events);
    oem_coolant_reset(&s->coolant);
    oem_mil_clear(&s->mil);
    oem_iat_reset(&s->iat);
    oem_vss_reset(&s->vss);
    oem_voltage_reset(&s->voltage);
    /*296C0 consumes the request. The preflight guarantees this succeeds;
       none of the included callbacks changes the record/config indices. */
    if (!oem_dtc_clear_worker(&s->events))
        return 0;
    oem_readiness_clear(&s->readiness); /*296C4, after request consumption */
    bind(s);
    return 1;
}
