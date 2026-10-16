#ifndef WIFI_REPLY_H_
#define WIFI_REPLY_H_

#include <stdint.h>
#include <stddef.h>

// Reply table of the WiFi command channel. The tag the module echoes in
// hdr.thread is a slot of this table, not a TX buffer number (a TX buffer is
// freed when it has been sent, long before the reply is there). A reply is
// delivered only if its sequence is the one the slot was claimed with.
// The functions are not locked: callers hold a critical section.

#define WIFI_REPLY_SLOTS    12

typedef struct {
    void *task;
    uint16_t sequence;
} wifi_reply_slot_t;

typedef struct {
    uint8_t thread;
    uint16_t sequence;
} wifi_reply_tag_t;

// Registers 'task' as the waiter of a free slot. Returns 0 if all are in use.
static inline int wifi_reply_claim(wifi_reply_slot_t *slots, uint16_t *next_sequence, void *task, wifi_reply_tag_t *tag)
{
    for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
        if (!slots[i].task) {
            slots[i].task = task;
            slots[i].sequence = (*next_sequence)++;
            tag->thread = (uint8_t)i;
            tag->sequence = slots[i].sequence;
            return 1;
        }
    }
    return 0;
}

// Returns the waiter of this reply and releases its slot, or NULL if there is
// none: the slot is free, or it belongs to another request.
static inline void *wifi_reply_take(wifi_reply_slot_t *slots, uint8_t thread, uint16_t sequence)
{
    if (thread >= WIFI_REPLY_SLOTS || !slots[thread].task || slots[thread].sequence != sequence) {
        return NULL;
    }
    void *task = slots[thread].task;
    slots[thread].task = NULL;
    return task;
}

// Gives up waiting. Returns 0 if the reply was taken in the meantime.
static inline int wifi_reply_abandon(wifi_reply_slot_t *slots, const wifi_reply_tag_t *tag)
{
    return wifi_reply_take(slots, tag->thread, tag->sequence) != NULL;
}

#endif /* WIFI_REPLY_H_ */
