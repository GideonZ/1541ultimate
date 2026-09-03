/*
 * gcr_track_bounds.h -- what a G64 says about its tracks, checked.
 *
 * The track lengths in a G64 file are not decoration: insert_disk() programs
 * them into the drive engine's parameter RAM, and floppy_mem.vhd both reads
 * and writes memory bounded by them. So a length taken from the file decides
 * how far the hardware reaches into a buffer, and a track that is not in the
 * image at all still has to be given a parameter pair the engine can survive.
 *
 * These are free functions over plain integers, deliberately: none of the
 * drive headers can be compiled on a build host, and this is the part that is
 * worth testing there. See software/drive/tests/.
 */
#ifndef DRIVE_GCR_TRACK_BOUNDS_H
#define DRIVE_GCR_TRACK_BOUNDS_H

#include <stdint.h>

/* A track table entry points at a two-byte length word followed by the track
 * data. Reading that word is itself an access into the image buffer, so it has
 * to lie inside the part of the buffer the file actually filled. Offset zero
 * means the track is absent. */
static inline bool gcr_track_header_is_readable(uint32_t offset, uint32_t bytes_read)
{
    if (offset == 0 || offset > bytes_read) {
        return false;
    }
    return (bytes_read - offset) >= 2;
}

/* Returns the track length to use, or 0 when the track cannot be used and has
 * to be left marked absent.
 *
 *   declared    the 16-bit word from the image, flag bits included
 *   offset      byte offset of that word within the buffer
 *   capacity    size of the buffer
 *   max_length  longest track the format allows
 *
 * The masked field holds 14 bits, so it admits lengths more than twice the
 * longest legitimate track, and a track near the end of the buffer can declare
 * a length that runs off it. Both are rejected here rather than programmed.
 */
static inline int gcr_validated_track_length(uint16_t declared, uint32_t offset,
                                             uint32_t capacity, int max_length)
{
    int length = (int)(declared & 0x3FFF);

    if (length <= 0 || length > max_length) {
        return 0;   /* zero would also divide by zero in insert_disk() */
    }
    if (offset > capacity || (capacity - offset) < 2) {
        return 0;
    }
    if ((uint32_t)length > (capacity - offset - 2)) {
        return 0;
    }
    return length;
}

#endif /* DRIVE_GCR_TRACK_BOUNDS_H */
