#include "ecu.h"
#include "faults.h"
#include "oem_history.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern u8 fake_eeprom[8192], fake_iac_response;
extern u32 fake_writes;
extern s32 fake_write_budget;
extern unsigned test_lifecycle(void);
static u8 flash[2][65536], backup[2][65536];
static s32 program_budget = -1;
static u8 erase_fail, read_fail;
static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
u8 hal_cal_read(u8 slot, u16 at, u8 *bytes, u8 n) {
    if (read_fail || slot > 1 || !n || n > 32 || at + n > 3104U) return 0;
    memcpy(bytes, flash[slot] + at, n); return 1;
}
u8 hal_cal_erase(u8 slot) {
    if (slot > 1 || !ecu.service) return 0;
    memset(flash[slot], 255, erase_fail ? 48U : 65536UL);
    return (u8)!erase_fail;
}
u8 hal_cal_program(u8 slot, u16 at, const u8 *bytes, u8 n) {
    u8 i;
    if (slot > 1 || !n || n > 32 || at + n > 3104U || (at & 1) || (n & 1) || !ecu.service) return 0;
    for (i = 0; i < n; i++) {
        if (!program_budget) return 0;
        if (program_budget > 0) program_budget--;
        if ((flash[slot][at + i] & bytes[i]) != bytes[i]) return 0;
        flash[slot][at + i] &= bytes[i];
    }
    return 1;
}
static void reset(void) {
    ecu_init(1);
    fake_iac_response = 0xC0;
    erase_fail = read_fail = 0;
    program_budget = fake_write_budget = -1;
}
static void finish(void) {
    unsigned i;
    for (i = 0; i < 250 && ecu.storage.phase; i++) storage_poll(100 + i);
    CHECK(!ecu.storage.phase && !ecu.storage_owner);
}
int main(void) {
    unsigned budget, i;
    static OemDiagnostics d, restored;
    OemHistory h;
    u8 byte = 1;
    reset(); memset(flash, 255, sizeof(flash)); memset(fake_eeprom, 255, sizeof(fake_eeprom));
    storage_load(); CHECK(!ecu.cal.valid);
    cal_example(ecu.cal.bytes[0]); ecu.cal.valid = 1; ecu.cal.generation = 1;
    CHECK(storage_save(100)); finish();
    CHECK(ecu.storage.result == 1 && !fake_writes);
    reset(); storage_load(); CHECK(ecu.cal.valid && ecu.cal.bytes[ecu.cal.active][0] == 60);
    memcpy(backup, flash, sizeof(flash));
    /* Every byte prefix, including the final two-byte commit word. */
    for (budget = 0; budget <= 3106; budget++) {
        memcpy(flash, backup, sizeof(flash)); reset(); storage_load();
        ecu.cal.bytes[ecu.cal.active][0] = 77; ecu.cal.generation++;
        program_budget = (s32)budget;
        CHECK(storage_save(100)); finish();
        CHECK(!fake_writes);
        reset(); storage_load();
        CHECK(ecu.cal.valid && cal_active()[0] == (budget == 3106 ? 77 : 60));
    }
    memcpy(flash, backup, sizeof(flash)); reset(); storage_load();
    erase_fail = 1; CHECK(storage_save(100)); finish();
    CHECK(ecu.storage.result == 3);
    reset(); storage_load(); CHECK(ecu.cal.valid && cal_active()[0] == 60);
    read_fail = 1; ecu.cal.valid = 0; storage_load(); CHECK(!ecu.cal.valid);
    reset(); storage_load();
    ecu.control.mode = ENGINE_RUNNING; CHECK(!storage_save(100));
    ecu.control.mode = ENGINE_STOPPED;
    CHECK(storage_save(100)); ecu.cal.generation++; finish();
    CHECK(ecu.storage.result == 2);
    /* Full OEM retained payload plus redundant standalone faults fit in 1KiB. */
    reset(); memset(&h, 0, sizeof(h)); memset(&d, 0, sizeof(d));
    CHECK(oem_history_encode(&d, h.payload));
    CHECK(oem_history_save(&h, &d, 100));
    for (i = 0; i < 100 && h.phase; i++) oem_history_poll(&h, 100 + i);
    CHECK(h.valid && h.result == 1);
    memset(&h, 0, sizeof(h)); memset(&restored, 0, sizeof(restored));
    CHECK(oem_history_load(&h, &restored));
    reset(); faults_load(); fault_set(FAULT_TPS, QUALITY_RANGE, 200);
    CHECK(faults_save(200));
    for (i = 0; i < 50 && faults.phase; i++) faults_poll(200 + i);
    CHECK(faults.result == 1);
    fault_set(FAULT_IAT, QUALITY_RANGE, 300); CHECK(faults_save(300));
    for (i = 0; i < 50 && faults.phase; i++) faults_poll(300 + i);
    CHECK(faults.result == 1);
    reset(); faults_load(); CHECK(faults.stored == ((1U << FAULT_TPS) | (1U << FAULT_IAT)));
    memset(&h, 0, sizeof(h)); CHECK(oem_history_load(&h, &restored));
    /* Single OEM snapshot: torn replacement is rejected, never reported valid. */
    fake_write_budget = 40; CHECK(oem_history_save(&h, &d, 400));
    for (i = 0; i < 100 && h.phase; i++) oem_history_poll(&h, 400 + i);
    CHECK(h.result == OEM_HISTORY_IO);
    reset(); memset(&h, 0, sizeof(h)); CHECK(!oem_history_load(&h, &restored));
    faults_load(); CHECK(faults.stored == ((1U << FAULT_TPS) | (1U << FAULT_IAT)));
    CHECK(!hal_eeprom_read(1024, &byte, 1) && !hal_eeprom_write(1024, &byte, 1));
    for (i = 1024; i < 8192; i++) CHECK(fake_eeprom[i] == 255);
    checks += test_lifecycle();
    printf("PASS %u stock-95080/NOR/lifecycle assertions, 3107 interrupted program prefixes\n", checks);
    return 0;
}
