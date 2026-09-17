#ifndef HEAP_TRACK_H
#define HEAP_TRACK_H

/*
 * Outstanding-allocation tracker.
 *
 * Built only when HEAP_TRACK is defined; without it every entry point below
 * compiles to nothing and the object carries no table, so a release image is
 * unaffected. Enable with:
 *
 *     make u64_no_esp EXTRA_DEFINES=-DHEAP_TRACK=1
 *
 * What it answers: "which allocations made since I reset it are still live?"
 * GET /v1/machine:heap gives the free-bytes figure that says a leak exists;
 * this says where it came from.
 *
 * The intended workflow is differential, because a live block is not a leaked
 * block: reset, exercise the device once, read; exercise it again, read again.
 * Allocations that are genuinely in use hold steady across the two, while
 * leaked ones grow by the same amount each time. tests/tools/heap_diff.py
 * does this.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *caller;
    uint32_t blocks;
    uint32_t bytes;
} HeapTrackCaller_t;

typedef struct {
    uint32_t live_blocks;
    uint32_t live_bytes;
    uint32_t evicted;   // records lost because a set was full; see heap_track.c
} HeapTrackTotals_t;

#ifdef HEAP_TRACK

void heap_track_record(void *p, size_t size, void *caller);
void heap_track_forget(void *p);
// operator new and malloc both funnel through one wrapper, so without this
// every block would be attributed to that wrapper rather than to its caller.
void heap_track_note_caller(void *p, void *caller);
void heap_track_reset(void);

// Fills `out` with one entry per distinct caller, largest byte total first,
// and returns how many were written. Totals cover every live block, including
// callers beyond `max`.
int heap_track_report(HeapTrackCaller_t *out, int max, HeapTrackTotals_t *totals);

#else

#define heap_track_record(p, size, caller)   ((void)0)
#define heap_track_forget(p)                 ((void)0)
#define heap_track_note_caller(p, caller)    ((void)0)
#define heap_track_reset()                   ((void)0)
#define heap_track_report(out, max, totals)  (0)

#endif

#ifdef __cplusplus
}
#endif

#endif /* HEAP_TRACK_H */
