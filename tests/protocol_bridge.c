#include "ecu.h"
#include <string.h>
extern u8 fake_tx[256];
extern u16 fake_tx_count;
void bridge_reset(void) {
    ecu_init(1);
    cal_example(ecu.cal.bytes[0]);
    ecu.cal.valid = 1;
    ecu.cal.generation = 1;
}
u16 bridge_exchange(const u8 *request, u16 count, u8 *response) {
    u16 i;
    fake_tx_count = 0;
    for (i = 0; i < count; i++)
        protocol_receive(request[i]);
    for (i = 0; i < 200; i++)
        protocol_poll(0);
    memcpy(response, fake_tx, fake_tx_count);
    return fake_tx_count;
}

void bridge_sync_losses(u32 losses) { ecu.rotation.losses = losses; }
/* Command 13 version 4 detail: a recovered IAC fault and a noise loss. */
void bridge_iac_detail(void) {
    ecu.iac.state = IAC_READY;
    ecu.iac.target = 300;
    ecu.iac.last_fault = 7;
    ecu.iac.retries = 2;
    ecu.iac.fault_count = 1000;
    ecu.iac.response = 0xC0;
    ecu.control.idle_mode = IDLE_FEEDBACK;
    ecu.control.idle_integral = -12345L;
    ecu.control.idle_spark10 = -35;
    ecu.rotation.loss_reason = ROT_LOSS_NOISE;
    ecu.rotation.loss_tooth = 11;
    ecu.authority.first_reason = INH_SYNC;
    timing_health.capture_resyncs = 0x1234;
    timing_health.capture_overruns = 3;
}
