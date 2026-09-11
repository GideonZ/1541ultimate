/*
 * Outstanding-allocation tracker. See heap_track.h for what it is for.
 *
 * Not built at all unless HEAP_TRACK is defined.
 */

#include "heap_track.h"

#ifdef HEAP_TRACK

#include <string.h>

/*
 * Sizing. 8192 slots at 16 bytes is 128 KB of BSS, which is affordable on a
 * diagnostic build and holds far more than a single run leaves live. Both are
 * overridable from the build:
 *
 *     EXTRA_DEFINES="-DHEAP_TRACK=1 -DHEAP_TRACK_SLOTS=16384"
 */
#ifndef HEAP_TRACK_SLOTS
#define HEAP_TRACK_SLOTS 8192
#endif

/* Smallest allocation worth recording. Zero tracks everything, which is what
   a hunt for many small objects needs; raise it to ignore the noise when
   chasing one big buffer. */
#ifndef HEAP_TRACK_MIN
#define HEAP_TRACK_MIN 0
#endif

/*
 * Slots are addressed directly from the pointer rather than searched, so
 * record and forget stay constant-time: they sit in the allocator path and a
 * linear scan there is not affordable once every allocation is tracked.
 *
 * Four ways per set, because a plain direct-mapped table lost a noticeable
 * number of records to collisions on real runs -- two live blocks landing in
 * one slot cost the older one. Four ways made that rare without making the
 * lookup a search. When a set is genuinely full the oldest way is dropped and
 * counted in `evicted`, so an under-reporting table says so rather than
 * quietly lying.
 */
#define HEAP_TRACK_WAYS 4
#define HEAP_TRACK_SETS (HEAP_TRACK_SLOTS / HEAP_TRACK_WAYS)

typedef struct {
    void *ptr;
    void *caller;
    uint32_t size;
    uint32_t seq;       // insertion order, so the oldest way can be identified
} Slot_t;

static Slot_t slots[HEAP_TRACK_SETS][HEAP_TRACK_WAYS];
static uint32_t evicted;
static uint32_t seq_counter;

static inline uint32_t set_of(void *p)
{
    uint32_t a = (uint32_t)p;
    // Allocations are aligned, so the low bits carry nothing; mixing in a
    // higher slice keeps neighbouring blocks from crowding one set.
    return ((a >> 3) ^ (a >> 15)) & (HEAP_TRACK_SETS - 1);
}

void heap_track_record(void *p, size_t size, void *caller)
{
    if (!p || size < HEAP_TRACK_MIN) {
        return;
    }
    Slot_t *set = slots[set_of(p)];
    int victim = 0;
    uint32_t oldest = 0xFFFFFFFFu;

    for (int i = 0; i < HEAP_TRACK_WAYS; i++) {
        if (set[i].ptr == NULL) {
            victim = i;
            goto claim;
        }
        if (set[i].seq < oldest) {
            oldest = set[i].seq;
            victim = i;
        }
    }
    evicted++;

claim:
    set[victim].ptr = p;
    set[victim].caller = caller;
    set[victim].size = (uint32_t)size;
    set[victim].seq = ++seq_counter;
}

void heap_track_forget(void *p)
{
    if (!p) {
        return;
    }
    Slot_t *set = slots[set_of(p)];
    for (int i = 0; i < HEAP_TRACK_WAYS; i++) {
        if (set[i].ptr == p) {
            set[i].ptr = NULL;
            return;
        }
    }
}

void heap_track_note_caller(void *p, void *caller)
{
    if (!p) {
        return;
    }
    Slot_t *set = slots[set_of(p)];
    for (int i = 0; i < HEAP_TRACK_WAYS; i++) {
        if (set[i].ptr == p) {
            set[i].caller = caller;
            return;
        }
    }
}

void heap_track_reset(void)
{
    memset(slots, 0, sizeof(slots));
    evicted = 0;
    seq_counter = 0;
}

int heap_track_report(HeapTrackCaller_t *out, int max, HeapTrackTotals_t *totals)
{
    int used = 0;
    uint32_t blocks = 0;
    uint32_t bytes = 0;

    for (int s = 0; s < HEAP_TRACK_SETS; s++) {
        for (int w = 0; w < HEAP_TRACK_WAYS; w++) {
            Slot_t *slot = &slots[s][w];
            if (!slot->ptr) {
                continue;
            }
            blocks++;
            bytes += slot->size;

            int i;
            for (i = 0; i < used; i++) {
                if (out[i].caller == slot->caller) {
                    break;
                }
            }
            if (i == used) {
                if (used == max) {
                    continue;   // totals still count it; the list is full
                }
                out[used].caller = slot->caller;
                out[used].blocks = 0;
                out[used].bytes = 0;
                used++;
            }
            out[i].blocks++;
            out[i].bytes += slot->size;
        }
    }

    // Largest first: the caller that matters is almost always at the top, and
    // the report is read by eye as often as by script.
    for (int i = 1; i < used; i++) {
        HeapTrackCaller_t key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].bytes < key.bytes) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }

    if (totals) {
        totals->live_blocks = blocks;
        totals->live_bytes = bytes;
        totals->evicted = evicted;
    }
    return used;
}

#endif /* HEAP_TRACK */
