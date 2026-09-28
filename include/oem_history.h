#ifndef LRE_OEM_HISTORY_H
#define LRE_OEM_HISTORY_H
#include "oem.h"
#define OEM_HISTORY_SIZE 768U
#define OEM_HISTORY_SCHEMA 2U
#define OEM_HISTORY_SLOT0 HISTORY_BASE0
#define OEM_HISTORY_SLOT1 HISTORY_BASE1
#if STOCK_95080
#if OEM_HISTORY_SIZE + 32U > FAULT_BASE0 || FAULT_BASE1 + 96U > EEPROM_BYTES
#error Stock EEPROM records exceed capacity
#endif
#else
#if OEM_HISTORY_SLOT0 < (32U + CAL_SIZE) || (OEM_HISTORY_SLOT0 + 32U + OEM_HISTORY_SIZE) > 4096U
#error History slot zero overlaps calibration storage
#endif
#if OEM_HISTORY_SLOT1 < (4096U + 32U + CAL_SIZE) ||                                                \
    (OEM_HISTORY_SLOT1 + 32U + OEM_HISTORY_SIZE) > 8192U
#error History slot one overlaps calibration storage
#endif
#endif
#define OEM_HISTORY_OK 1U
#define OEM_HISTORY_TIMEOUT 2U
#define OEM_HISTORY_IO 3U
#define OEM_HISTORY_INVALID 4U
/* Standalone EEPROM journal, independent of the calibration journal. The
   payload is an immutable snapshot, never a pointer to changing live state. */
typedef struct {
    u8 payload[OEM_HISTORY_SIZE], header[32], verify[32];
    u32 sequence, deadline;
    u16 offset;
    u8 phase, slot, active_slot, valid, result;
} OemHistory;
u8 oem_history_encode(const OemDiagnostics *s, u8 *payload);
u8 oem_history_validate(const u8 *payload);
u8 oem_history_decode(OemDiagnostics *s, const u8 *payload);
/* Load only at boot, with outputs/interrupts disabled and EEPROM available.
   A failed load leaves diagnostic state unchanged; the owner decides the
   native lost-history startup path. Save enters the existing stopped service
   latch; the owner must schedule poll and retain ECU power through completion. */
u8 oem_history_load(OemHistory *h, OemDiagnostics *s);
u8 oem_history_save(OemHistory *h, const OemDiagnostics *s, u32 now);
void oem_history_poll(OemHistory *h, u32 now);
/* Persistence bookkeeping for a single foreground diagnostic owner. Initialize
   to zero at reset. Do not mix direct journal calls with these owner calls.
   Native initialization, reset-cause and shutdown policy remain caller-owned. */
typedef struct {
    OemHistory journal;
    u8 ready, dirty, changed, clear_pending, clear_snapshot, clear_durable;
} OemHistoryOwner;
u8 oem_history_owner_load(OemHistoryOwner *h, OemDiagnostics *s);
/* Call after every retained-state mutation, including native initialization.
   Call cleared only AFTER the accepted native clear worker has completed. */
void oem_history_owner_changed(OemHistoryOwner *h);
void oem_history_owner_cleared(OemHistoryOwner *h);
#define OEM_CLEAR_ALL 0U
#define OEM_CLEAR_EMISSIONS 1U
#define OEM_CLEAR_EVENT 2U
/* Foreground-only admission and service of the ported clear projection.
   Admission dirties retained descriptors; only service marks a completed
   clear pending persistence. Neither call acknowledges an OEM tool request. */
u8 oem_history_owner_clear_request(OemHistoryOwner *h, OemDiagnostics *s, u8 kind, u8 event);
u8 oem_history_owner_clear_service(OemHistoryOwner *h, OemDiagnostics *s);
u8 oem_history_owner_save(OemHistoryOwner *h, const OemDiagnostics *s, u32 now);
void oem_history_owner_poll(OemHistoryOwner *h, u32 now);
/* History only: this is not permission to release the ECU power latch. */
u8 oem_history_owner_settled(const OemHistoryOwner *h);
#endif
