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

/* The low half of a parameter word is the last valid offset in the track, so
 * this is a 257-byte empty track. init() and remove_disk() already program it
 * for a drive with nothing in it. */
#define GCR_EMPTY_TRACK_PARAM   0x100

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
 *   bytes_read  how much of the buffer the file actually filled
 *   max_length  longest track the format allows
 *
 * The bound is what was read, not how large the buffer is. A truncated image
 * can declare a track whose header is present but whose data never arrived,
 * and the buffer behind it holds whatever the previous mount left there. So
 * the whole track, header word included, has to lie inside what was read:
 * offset + 2 + length <= bytes_read, written as subtractions so that no sum
 * can wrap.
 *
 * The masked field holds 14 bits, so it also admits lengths more than twice
 * the longest legitimate track. Rejected here rather than programmed.
 */
static inline int gcr_validated_track_length(uint16_t declared, uint32_t offset,
                                             uint32_t bytes_read, int max_length)
{
    int length = (int)(declared & 0x3FFF);

    if (length <= 0 || length > max_length) {
        return 0;   /* zero would also divide by zero in insert_disk() */
    }
    if (offset > bytes_read || (bytes_read - offset) < 2) {
        return 0;
    }
    if ((uint32_t)length > (bytes_read - offset - 2)) {
        return 0;
    }
    return length;
}

/* An MFM track carries a metadata area at its front: a sector count, a version
 * byte, and five bytes for each of up to WD_MAX_SECTORS_PER_TRACK sectors.
 * map_gcr_image_to_mfm() reads that area whole for a track marked MFM, and
 * reserves what lies behind it for sector data. Neither exists unless the
 * track is at least as long as the area itself.
 *
 * Length and MFM marker share one word in a G64, so 0x8001 -- a one-byte track
 * marked MFM -- is a legal thing to put in an image. The declared extent then
 * checks out: one byte was delivered. The mapping used to believe the marker
 * anyway, read the full metadata area from that one byte, and take the space
 * behind it as a subtraction in an unsigned field, where it cannot come out
 * negative.
 *
 *   track_length  the validated length of the track
 *   header_size   bytes of metadata at the front of an MFM track
 */
static inline bool gcr_track_can_hold_mfm_header(int track_length, int header_size)
{
    if (header_size <= 0 || track_length <= 0) {
        return false;
    }
    return track_length >= header_size;
}

/* Bytes behind the metadata area, or 0 for a track with no room for one. The
 * caller keeps this in an unsigned field that gates every track write, so it
 * must never be reached by underflow. */
static inline uint32_t gcr_mfm_reserved_space(int track_length, int header_size)
{
    if (!gcr_track_can_hold_mfm_header(track_length, header_size)) {
        return 0;
    }
    return (uint32_t)(track_length - header_size);
}

/* Builds one entry of the drive's track parameter RAM: the address the engine
 * works at, and a word carrying the last valid offset in the low half and the
 * bit time in the high half.
 *
 * A track that is not in the image gets the dummy track together with the same
 * short, safe length init() and remove_disk() use -- not whatever length the
 * previous track happened to leave in a local.
 */
static inline void gcr_track_parameters(uint32_t track_address, int track_length,
                                        uint32_t dummy_address, int dummy_length,
                                        uint32_t rotation_speed,
                                        uint32_t *out_address, uint32_t *out_param)
{
    if (track_address != 0 && track_length > 0) {
        uint32_t bit_time = rotation_speed / (uint32_t)track_length;
        *out_address = track_address;
        *out_param   = (uint32_t)(track_length - 1) | (bit_time << 16);
        return;
    }

    uint32_t bit_time = (dummy_length > 0) ? (rotation_speed / (uint32_t)dummy_length) : 0;
    *out_address = dummy_address;
    *out_param   = GCR_EMPTY_TRACK_PARAM | (bit_time << 16);
}

#endif /* DRIVE_GCR_TRACK_BOUNDS_H */
