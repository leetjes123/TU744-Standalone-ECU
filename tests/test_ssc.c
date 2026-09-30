/* Compile the actual board SSC owner with observable register substitutes.
   Peripheral clocks and interrupts are supplied explicitly by each scenario. */
#include "ecu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define LRE_BOARD_H
#define BOARD_RELEASED 1
#define IRQ_HANDLER(vector)
Ecu ecu;
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        assert(x);                                                                                 \
    } while (0)
static u16 T7, CC22, CCM5, CC22IE, CC22IR, SSCCON, SSCRIE, SSCRB, SSCTIR;
static u16 SSCTE, SSCRE, SSCPE, SSCBE;
static u16 rx_flag, tx_buffer, irq_enabled = 1;
static u8 PIN_EEPROM_SELECT, PIN_IAC_SELECT;
static u8 automatic_rx, pending_byte, response_byte;
static u32 bytes_started;
static u16 compare_advance;
static u16 *compare_register(void) {
    /* Peripheral time can pass between watch() and programming the compare. */
    if (compare_advance) {
        T7 = (u16)(T7 + compare_advance);
        compare_advance = 0;
        CC22IR = 1;
    }
    return &CC22;
}
static u16 *tx_register(void) {
    CHECK(!pending_byte);
    CHECK(PIN_EEPROM_SELECT != PIN_IAC_SELECT);
    pending_byte = 1;
    bytes_started++;
    return &tx_buffer;
}
static u16 *receive_flag(void) {
    if (automatic_rx && pending_byte) {
        SSCRB = response_byte;
        pending_byte = 0;
        rx_flag = 1;
    }
    return &rx_flag;
}
#define SSCRIR (*receive_flag())
#define SSCTB (*tx_register())
u16 hal_lock(void) {
    u16 previous = irq_enabled;
    irq_enabled = 0;
    return previous;
}
void hal_unlock(u16 previous) {
    irq_enabled = previous;
}
#define CC22 (*compare_register())
#include "../target/c167/ssc.c"
#undef CC22

static void setup(void) {
    memset(&ecu, 0, sizeof(ecu));
    memset(&hold, 0, sizeof(hold));
    ecu.key_input = ecu.board_released = 1;
    owned = SSC_FREE;
    SSCTE = SSCRE = SSCPE = SSCBE = 0;
    last_iac_command = 0x3F;
    progress_clock = 0;
    progress_stamp = 0;
    T7 = CC22 = CC22IE = CC22IR = SSCCON = SSCRIE = SSCRB = SSCTIR = rx_flag = 0;
    CCM5 = 0xA00B;
    irq_enabled = 1;
    PIN_EEPROM_SELECT = PIN_IAC_SELECT = 1;
    automatic_rx = pending_byte = 0;
    response_byte = 0xC0;
    bytes_started = 0;
    compare_advance = 0;
}
static void complete(u16 elapsed, u8 response) {
    CHECK(pending_byte);
    pending_byte = 0;
    SSCRB = response;
    rx_flag = 0; /* Hardware clears IR on vector entry. */
    T7 = (u16)(T7 + elapsed);
    ssc_receive_isr();
}
static void test_waveform(void) {
    u8 phase, polarity, value;
    u16 edge, k;
    static const u8 phases[4] = {0x12, 0x13, 0x1B, 0x1A};
    for (phase = 0; phase < 4; phase++) {
        setup();
        T7 = 65400;
        polarity = phases[phase] & 9U;
        CHECK(hal_iac_hold_start(phases[phase]));
        CHECK(owned == SSC_HOLD && hold.state == HOLD_WAIT && CC22IE);
        CHECK((CCM5 & 0xF0FFU) == 0xA00BU);
        for (k = 0; k < 200; k++) {
            edge = hold.stamp;
            CHECK(!hal_eeprom_read(0, &value, 1));
            CHECK(hal_eeprom_busy() == 2);
            CHECK(!hal_eeprom_write(0, &value, 1));
            T7 = CC22;
            iac_compare_isr();
            CHECK(owned == SSC_HOLD && hold.state == HOLD_RX && SSCRIE);
            CHECK((tx_buffer & 9U) == polarity);
            CHECK((tx_buffer & 0x36U) == ((k & 1) ? 0x24U : 0x12U));
            CHECK(!PIN_IAC_SELECT && PIN_EEPROM_SELECT && !CC22IE);
            /* Repeated hold open flags are not motion qualification. */
            complete(HOLD_SPI_TICKS, k & 1 ? 0x40 : 0xC0);
            CHECK((u16)(T7 - edge) == ((k & 1) ? HOLD_HIGH_TICKS : HOLD_LOW_TICKS));
            CHECK(!hold.fault && hold.state == HOLD_WAIT && PIN_IAC_SELECT);
        }
        automatic_rx = 1;
        CHECK(hal_iac_transfer(0x3F, &value));
        CHECK(owned == SSC_FREE && !SSCRIE && !CC22IE);
        CHECK(PIN_IAC_SELECT && PIN_EEPROM_SELECT);
        CHECK(hal_eeprom_read(0, &value, 1));
        CHECK(irq_enabled == 1);
    }
}
static void test_handoff(void) {
    u8 response;
    setup();
    CHECK(hal_iac_hold_start(0x13));
    T7 = CC22;
    iac_compare_isr();
    CHECK(pending_byte);
    automatic_rx = 1;
    /* A new motion command drains the old byte before selecting a new frame. */
    CHECK(hal_iac_transfer(0x1B, &response));
    CHECK(bytes_started == 2 && tx_buffer == 0x1B && !pending_byte);
    CHECK(owned == SSC_FREE && hold.state == HOLD_IDLE && !hold.enabled);
    CHECK(!CC22IE && !SSCRIE && !hold.fault);
    CHECK(hal_eeprom_write(0, &response, 1));

    setup();
    CHECK(hal_iac_hold_start(0x12));
    T7 = CC22;
    iac_compare_isr();
    CHECK(!hal_iac_transfer(0x1B, &response)); /* no RX yet: bounded handoff */
    CHECK(owned == SSC_HOLD && bytes_started == 1 && pending_byte);
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(owned == SSC_FREE && !CC22IE && !SSCRIE);
    automatic_rx = 1;
    CHECK(hal_iac_transfer(0x3F, &response));
}
static void test_faults(void) {
    u8 response;
    setup();
    CHECK(hal_iac_hold_start(0x12));
    T7 = (u16)(CC22 + HOLD_LATE_TICKS + 1);
    board_ssc_tick();
    CHECK(hold.fault == 7 && tx_buffer == 0x3F && hold.state == HOLD_RX);
    CHECK(!hal_eeprom_read(0, &response, 1));
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(owned == SSC_FREE && !hold.off_needed && hold.fault == 7);
    CHECK(!hal_iac_hold_start(0x12));

    setup();
    CHECK(hal_iac_hold_start(0x12));
    T7 = CC22;
    iac_compare_isr();
    T7 = (u16)(T7 + HOLD_RX_TICKS + 1);
    SSCCON = 0x1000; /* stuck shift engine */
    board_ssc_tick();
    CHECK(hold.fault == 1 && hold.off_needed && owned == SSC_HOLD);
    CHECK(PIN_IAC_SELECT && !SSCRIE && !CC22IE);
    CHECK(!hal_eeprom_read(0, &response, 1));
    board_ssc_tick();
    CHECK(bytes_started == 1); /* never overwrite a busy peripheral */
    pending_byte = 0;
    SSCCON = 0; /* hardware becomes idle after aborted frame */
    board_ssc_tick();
    CHECK(tx_buffer == 0x3F && owned == SSC_HOLD);
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(owned == SSC_FREE && hold.fault == 1);

    setup();
    CHECK(hal_iac_hold_start(0x12));
    T7 = CC22;
    iac_compare_isr();
    complete(HOLD_SPI_TICKS, 0x80);
    CHECK(hold.fault == 3 && tx_buffer == 0x3F && pending_byte);
    complete(HOLD_SPI_TICKS, 0);
    CHECK(hold.fault == 3 && owned == SSC_FREE); /* first cause retained */
    CHECK(hal_iac_hold_response() == 0x80);      /* off reply cannot erase evidence */

    setup();
    CHECK(hal_iac_hold_start(0x12));
    ecu.authority.inhibits = INH_DEADLINE;
    board_ssc_tick();
    CHECK(hold.fault == 7 && tx_buffer == 0x3F);
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(owned == SSC_FREE);

    setup();
    CHECK(hal_iac_hold_start(0x12));
    ecu.service = 1;
    board_ssc_tick();
    CHECK(!hold.fault && tx_buffer == 0x3F);
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(owned == SSC_FREE && !hold.enabled);

    setup();
    automatic_rx = 1;
    CHECK(hal_iac_transfer(0x12, &response));
    automatic_rx = 0;
    ecu.authority.inhibits = INH_DEADLINE;
    board_ssc_tick();
    CHECK(owned == SSC_HOLD && tx_buffer == 0x3F && hold.fault == 7);
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(last_iac_command == 0x3F && owned == SSC_FREE);

    setup();
    CHECK(hal_iac_hold_start(0x12));
    T7 = CC22;
    iac_compare_isr();
    SSCPE = 1;
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(hold.fault == 1 && !SSCPE && tx_buffer == 0x3F);
    CHECK(hal_iac_hold_response() == 0xC0);
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(owned == SSC_FREE);

    setup();
    T7 = 1234;
    CHECK(hal_iac_hold_start(0x12));
    ecu.milliseconds = 2; /* T1 clock advances while T7 is stuck. */
    board_ssc_tick();
    CHECK(hold.fault == 7 && tx_buffer == 0x3F);
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(owned == SSC_FREE);

    setup();
    CHECK(hal_iac_hold_start(0x12));
    T7 = CC22;
    iac_compare_isr();
    ecu.milliseconds = 2;
    board_ssc_tick(); /* establish a new independent progress sample */
    SSCCON = 0x1000;
    ecu.milliseconds = 4; /* frozen T7 during a peripheral transfer */
    board_ssc_tick();
    CHECK(hold.fault == 7 && hold.off_needed && owned == SSC_HOLD);
    CHECK(PIN_IAC_SELECT && !SSCRIE && !CC22IE && bytes_started == 1);
    CHECK(!hal_eeprom_read(0, &response, 1));
    pending_byte = 0;
    SSCCON = 0;
    board_ssc_tick();
    CHECK(tx_buffer == 0x3F && pending_byte);
    complete(HOLD_SPI_TICKS, 0xC0);
    CHECK(owned == SSC_FREE && hold.fault == 7);
}
static void test_compare_rearm_race(void) {
    u8 wrap;
    for (wrap = 0; wrap < 2; wrap++) {
        setup();
        T7 = wrap ? (u16)(65536UL - (HOLD_LOW_TICKS - HOLD_SPI_TICKS)) : 100U;
        CHECK(hal_iac_hold_start(0x12));
        T7 = (u16)(hold.due - 1U);
        compare_advance = 3;
        board_ssc_tick();
        CHECK(CC22IR && CC22IE && hold.state == HOLD_WAIT && !hold.fault);
        CC22IR = 0; /* interrupt acceptance */
        iac_compare_isr();
        CHECK(hold.state == HOLD_RX && pending_byte && !hold.fault);
        complete(HOLD_SPI_TICKS, 0xC0);
        CHECK(hold.state == HOLD_WAIT && !hold.fault);
    }
}
int main(void) {
    u8 byte = 0;
    setup();
    CHECK(!hal_eeprom_read(EEPROM_BYTES, &byte, 1));
    CHECK(!hal_eeprom_write(EEPROM_BYTES, &byte, 1));
    CHECK(!hal_eeprom_read(EEPROM_BYTES - 1U, &byte, 2));
    CHECK(!hal_eeprom_write(EEPROM_BYTES - 1U, &byte, 2));
    test_waveform();
    test_handoff();
    test_faults();
    test_compare_rearm_race();
    printf("PASS %u SSC owner/register assertions\n", checks);
    return 0;
}
