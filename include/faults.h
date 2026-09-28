#ifndef LRE_FAULTS_H
#define LRE_FAULTS_H
#include "ecu.h"
/* Standalone DTC IDs 1..6, not SAE codes or OEM event numbers. */
#define FAULT_CAL 0U
#define FAULT_TPS 1U
#define FAULT_CLT 2U
#define FAULT_IAT 3U
#define FAULT_BATTERY 4U
#define FAULT_MAP 5U
#define FAULT_COUNT 6U
#define FAULT_SLOT0 FAULT_BASE0
#define FAULT_SLOT1 FAULT_BASE1
#define FAULT_BYTES 96U
typedef struct {
    u8 reason;
    u16 occurrences;
    u32 first, last;
} FaultRecord;
typedef struct {
    FaultRecord record[FAULT_COUNT];
    u16 active, stored;
    u8 dirty, changed, loaded, valid, slot, target, phase, result, attempted;
    u8 image[FAULT_BYTES], verify[32];
    u16 offset;
    u32 sequence, deadline;
} FaultState;
extern FaultState faults;
void faults_init(void);
void fault_set(u8 id, u8 reason, u32 now);
void faults_load(void);
u8 faults_save(u32 now);
void faults_poll(u32 now);
u8 faults_settled(void);
void sensor_faults(u32 now);
/* Tool clear: forget stored records that are no longer active. */
void faults_clear(void);
#endif
