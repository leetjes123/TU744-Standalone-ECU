#include "oem.h"
#include "oem_vss_data.h"

void oem_vss_reset(OemVss *s) {
    /* 2A298: clearing does not reset source qualification or shared flags. */
    if (s->descriptor & 128U) {
        s->fail_count = s->pass_count = 0;
        s->status &= 0xFFFBU;
    }
}

u16 oem_vss_update(OemVss *s) {
    u16 descriptor = s->descriptor;
    u8 failing;
    /* 2A01E..2A282, before event68 ingestion. B24C is loaded by the ROM
       prologue but is never tested. No missing source is assumed healthy. */
    if (s->source >= 3U ||
        (s->source == 1U &&
         (s->source_a_status >= 2U || (s->source_a_descriptor & 0x0F00U) == 0x0800U)) ||
        (s->source == 2U &&
         (s->source_b_status >= 2U || (s->source_b_descriptor & 0x0F00U) == 0x0800U))) {
        if (s->source_count) {
            s->source_count--;
            s->fd08 &= 0xFFFDU;
        } else
            s->fd08 |= 2U;
    } else {
        s->source_count = OEM_VSS_COUNT;
        s->fd08 &= 0xFFFDU;
    }
    if (s->speed >= OEM_VSS_HIGH)
        s->fd08 |= 8U;
    else
        s->fd08 &= 0xFFF7U;
    if (s->speed < OEM_VSS_LOW)
        s->fd08 |= 16U;
    else
        s->fd08 &= 0xFFEFU;
    if (s->coolant > OEM_VSS_COOLANT && s->condition_speed < OEM_VSS_SPEED_A * 160U &&
        s->engine_speed > OEM_VSS_ENGINE_A && s->engine_speed < OEM_VSS_ENGINE_MAX &&
        (s->fd18 & 32U))
        s->fd06 |= 0x8000U;
    else
        s->fd06 &= 0x7FFFU;
    if (s->coolant > OEM_VSS_COOLANT && s->condition_speed < OEM_VSS_SPEED_B * 160U &&
        s->engine_speed > OEM_VSS_ENGINE_B && (s->fd52 & 8U) && s->load > OEM_VSS_LOAD)
        s->fd08 |= 1U;
    else
        s->fd08 &= 0xFFFEU;
    failing = (u8)((s->fd06 & 0x8000U) || (s->fd08 & 1U));
    if (failing) {
        /* Preserve the native increment-before-compare, including byte wrap. */
        s->fail_count++;
        if (s->fail_count > OEM_VSS_COUNT) {
            s->fail_count--;
            s->status |= 1U;
        } else
            s->status &= 0xFFFEU;
    } else {
        s->fail_count = 0;
        s->status &= 0xFFFEU;
    }
    if (s->condition_speed >= OEM_VSS_SPEED_A * 160U) {
        s->pass_count++;
        if (s->pass_count > OEM_VSS_COUNT) {
            s->pass_count--;
            s->status |= 2U;
        } else
            s->status &= 0xFFFDU;
    } else {
        s->pass_count = 0;
        s->status &= 0xFFFDU;
    }
    if (!(s->fd06 & 256U)) {
        descriptor &= 0xF0FEU;
        if (s->fd08 & 2U)
            descriptor |= 0x0801U;
        if (s->fd5e & 0x2000U)
            descriptor |= 0x2002U;
    } else {
        if (s->status & 2U) {
            s->status &= 0xFFFBU;
            descriptor |= 0x2002U;
        } else if (s->status & 1U) {
            s->status |= 4U;
            descriptor |= 0x2002U;
        }
        descriptor &= 0xF0FEU;
        if (s->status & 4U)
            descriptor |= failing ? 0x0401U : 0x0801U;
    }
    return descriptor;
}
