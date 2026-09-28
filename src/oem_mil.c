#include "oem.h"
/* TU5JP 0x6C1B0..0x6C3CA, stock 0x198EC=0, 0x198ED=15.
   Preserve native state bits. There is deliberately no flashing routine. */
void oem_mil_update(OemMil *s) {
    if (s->fd6a & 64U) {
        if (s->prove_count)
            s->prove_count--;
        else {
            s->prove_flags |= 1;
            s->state &= 0xFEU;
        }
    } else {
        s->prove_flags &= 0xFEU;
        s->prove_count = 15;
    }
    if (!(s->fd08 & 8192U))
        s->lamp = 0;
    s->state &= 0xEFU;
    if (s->demand == 1 || s->demand == 2 || (s->fd0e & 16U) || (s->fd12 & 64U) || (s->state & 6U)) {
        if (s->prove_flags & 1U)
            s->retained |= 1;
        else
            s->retained &= 0xFEU;
        s->state |= 64;
    } else {
        s->retained &= 0xFEU;
        s->state &= 0xBFU;
    }
    if (!s->demand && !(s->state & 2U) && !(s->fd5a & 8U) && !(s->fd0e & 24U) && !(s->state & 1U) &&
        !(s->fd12 & 64U)) {
        s->lamp = 0;
        s->state &= 0xF3U;
        return;
    }
    if (s->state & 1U)
        return;
    if (s->demand == 1) {
        s->state |= 2;
        s->state &= 0xFBU;
    } else if (s->demand == 2)
        s->state |= 4;
    else
        s->state &= 0xFBU;
    if ((s->state & 4U) || (s->fd0e & 24U)) {
        s->state |= 8;
        return;
    }
    if ((s->state & 2U) || (s->fd5a & 8U) || (s->fd12 & 64U)) {
        if (!(s->state & 16U) && (s->fd08 & 8192U))
            s->lamp = 1;
    }
    s->state &= 0xF7U;
}
void oem_mil_on(OemMil *s) {
    s->lamp = 1;
    if (s->demand == 1 || s->demand == 2 || (s->fd0e & 16U) || (s->fd12 & 64U))
        s->state |= 64;
    s->prove_count = 15;
    s->state |= 1;
    s->state &= 0xF7U;
    s->flash_count = 0;
}
void oem_mil_off(OemMil *s) {
    if ((s->state & 2U) && !s->demand)
        s->state &= 0xF9U;
    s->lamp = 0;
}
void oem_mil_clear(OemMil *s) {
    s->state &= 0xB9U;
    s->retained &= 0xFEU;
}
