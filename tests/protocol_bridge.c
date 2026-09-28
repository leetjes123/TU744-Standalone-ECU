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
