#include "ecu.h"
#include "lifecycle.h"
#include <string.h>
u8 fake_eeprom[8192], fake_injectors[4], fake_coils[2];
u8 fake_iac_ok = 1, fake_iac_response = 0xC0, fake_write_fail = 0;
u8 fake_eeprom_status, fake_eeprom_read_fail;
u8 fake_iac_command, fake_fan, fake_heaters;
u8 fake_hold_fault, fake_hold_active, fake_hold_high;
u32 fake_iac_transfers;
u16 fake_irq = 1, fake_injector_ticks[4];
u32 fake_starts = 0, fake_writes = 0;
s32 fake_write_budget = -1;
u32 fake_watchdog_services;
u8 fake_tx[256];
u16 fake_tx_count;
u8 fake_run_permission = 1, fake_power_release_allowed;
u32 fake_system_resets;
u8 hal_run_permission(void) { return fake_run_permission; }
u8 hal_power_release(void) { return fake_power_release_allowed; }
u32 hal_capture_clock(void) { return ecu.milliseconds * 1250UL; }
void hal_system_reset(void) { fake_system_resets++; }
u16 hal_lock(void) {
    u16 old = fake_irq;
    fake_irq = 0;
    return old;
}
void hal_unlock(u16 state) {
    fake_irq = state;
}
void hal_cancel_fuel(void) {
    memset(fake_injectors, 0, 4);
}
void hal_cancel_spark(void) {
    memset(fake_coils, 0, 2);
    ecu.authority.spark_draining = 0;
}
void hal_revoke_spark(u8 lost_angle) {
    (void)lost_angle;
    ecu.authority.spark_draining = (u8)(ecu.authority.coil_active[0] | (ecu.authority.coil_active[1] << 1));
}
void hal_cancel_all(void) {
    hal_cancel_fuel();
    hal_cancel_spark();
}
u8 hal_injector_start(u8 ch, u16 ticks) {
    if (fake_irq)
        return 0;
    fake_injectors[ch] = 1;
    fake_injector_ticks[ch] = ticks;
    fake_starts++;
    return 1;
}
u8 hal_coil_start(u8 ch, u16 ticks) {
    (void)ticks;
    if (fake_irq)
        return 0;
    fake_coils[ch] = 1;
    fake_starts++;
    return 1;
}
void hal_coil_off(u8 ch) {
    fake_coils[ch] = 0;
    ecu.authority.spark_draining &= (u8)~(1U << ch);
}
void hal_phase_arm(void) {}
void hal_phase_disarm(void) {}
void hal_aux(u8 a, u8 b, u8 c, u8 d, u8 e, u8 f) {
    (void)a;
    fake_fan = b;
    (void)c;
    (void)d;
    fake_heaters = e;
    (void)f;
}
u8 hal_iac_transfer(u8 command, u8 *response) {
    fake_hold_active = 0;
    fake_iac_command = command;
    fake_iac_transfers++;
    *response = fake_iac_response;
    return fake_iac_ok;
}
u8 hal_iac_hold_start(u8 high_command) {
    fake_hold_high = high_command;
    fake_hold_active = fake_iac_ok;
    return fake_iac_ok;
}
u8 hal_iac_hold_fault(void) {
    return fake_hold_fault;
}
u8 hal_iac_hold_response(void) {
    return fake_iac_response;
}
u8 hal_eeprom_read(u16 at, u8 *b, u8 n) {
    if (fake_eeprom_read_fail || at > EEPROM_BYTES || n > EEPROM_BYTES - at)
        return 0;
    memcpy(b, fake_eeprom + at, n);
    return 1;
}
u8 hal_eeprom_write(u16 at, const u8 *b, u8 n) {
    fake_writes++;
    if (fake_write_fail || at > EEPROM_BYTES || n > EEPROM_BYTES - at || ((at & 31U) + n) > 32U)
        return 0;
    if (fake_write_budget >= 0) {
        if (fake_write_budget < n) {
            memcpy(fake_eeprom + at, b, (u16)fake_write_budget);
            fake_write_budget = 0;
            return 0;
        }
        fake_write_budget -= n;
    }
    memcpy(fake_eeprom + at, b, n);
    return 1;
}
u8 hal_eeprom_busy(void) {
    return fake_eeprom_status;
}
void hal_watchdog_service(void) {
    fake_watchdog_services++;
}
void hal_uart_send(u8 byte) {
    if (fake_tx_count < 256)
        fake_tx[fake_tx_count++] = byte;
}
u16 fake_firmware_updates;
void hal_firmware_update(void) {
    fake_firmware_updates++;
}
u8 hal_uart_ready(void) {
    return 1;
}
