#include "board.h"
#include "oem_runtime.h"
#include "faults.h"
void main(void) {
    board_init();
    ecu_init(BOARD_RELEASED);
    /* A reset need not reset an externally powered stepper driver. Send off
       before EEPROM traffic, including on the development-gated build. */
    iac_disable();
    /* Knock IC: the OEM pin timeline from reset (port init, hold, release).
       Before EEPROM traffic, so the storage load cannot delay it. */
    board_knock_ic_boot(KNOCK_FILTER_KHZ_DEFAULT);
    board_knock_init();
    if (!ecu.iac.off_pending) {
        faults_load();
        storage_load();
        fault_set(FAULT_CAL, ecu.cal.valid ? 0 : 1, 0);
        oem_runtime_boot();
    }
    __asm {EINIT}
    hal_watchdog_service();
    IEN = 1;
    for (;;)
        ecu_poll();
}
