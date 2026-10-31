/*
 * radio_boot.c
 *
 * See radio_boot.h. Free of ESP-IDF, so that the host test tests what ships.
 */

#include "radio_boot.h"

void radio_record_set(radio_record_t *rec, bool on)
{
    uint32_t state = on ? RADIO_RECORD_ON : RADIO_RECORD_OFF;
    // The magic goes last, so that a record cut short by a reset does not check out;
    // volatile keeps the stores in that order
    volatile radio_record_t *v = rec;
    v->magic = 0;
    v->state = state;
    v->check = ~(RADIO_RECORD_MAGIC ^ state);
    v->magic = RADIO_RECORD_MAGIC;
}

static bool record_valid(const radio_record_t *rec)
{
    return (rec->magic == RADIO_RECORD_MAGIC) &&
           ((rec->state == RADIO_RECORD_ON) || (rec->state == RADIO_RECORD_OFF)) &&
           (rec->check == ~(rec->magic ^ rec->state));
}

radio_boot_t radio_boot_decision(bool self_restart, const radio_record_t *rec)
{
    if (!self_restart) {
        return RADIO_BOOT_WAIT;
    }
    if (!record_valid(rec)) {
        return RADIO_BOOT_ON; // no record: the radio goes on, as it always did
    }
    return (rec->state == RADIO_RECORD_ON) ? RADIO_BOOT_ON : RADIO_BOOT_OFF;
}
