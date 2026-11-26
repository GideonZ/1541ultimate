/*
 * recovery_image.h
 *
 * Reads an update image (.ue2 or .cfw) as it streams in, and hands each part
 * of it to the caller with the address it is to be loaded at. Nothing is
 * buffered beyond a record header, so an image of any size can be loaded
 * straight from the network into the memory of the board.
 *
 * An image is what tools/hex2bin -r writes: a sequence of records, each a
 * little endian header { load, length, start } followed by `length` bytes,
 * padded to a multiple of four. A .cfw is the same, after a 36 byte header:
 * the fingerprint 0x4115ADDE, a 16 byte product family and a 16 byte product
 * name. The last non-zero start in the image is where it is started.
 *
 * The reader does not hash: the caller hashes the raw stream for the manifest,
 * and must not start the image before that hash is checked.
 *
 * Free of ESP-IDF, so software/test/recovery_image tests what ships.
 */

#ifndef SOFTWARE_U64CTRL_MAIN_RECOVERY_IMAGE_H_
#define SOFTWARE_U64CTRL_MAIN_RECOVERY_IMAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECOVERY_IMAGE_FINGERPRINT 0x4115ADDEu
#define RECOVERY_IMAGE_PREHEADER   36
#define RECOVERY_IMAGE_RECORD_HDR  12

typedef enum {
    RECOVERY_IMAGE_OK = 0,
    RECOVERY_IMAGE_ERR_FAMILY,     // a .cfw for another product family
    RECOVERY_IMAGE_ERR_RANGE,      // a record or the start outside the allowed window
    RECOVERY_IMAGE_ERR_WRITE,      // the write callback failed
    RECOVERY_IMAGE_ERR_TRUNCATED,  // the stream ended inside a header or a record
    RECOVERY_IMAGE_ERR_NO_RECORDS,
    RECOVERY_IMAGE_ERR_NO_START,   // no record names a start address
    RECOVERY_IMAGE_ERR_FAILED,     // an earlier call already failed
} recovery_image_result_t;

/* Stores `len` bytes at `addr`. Called with consecutive addresses within a
   record, in pieces of any size. Returns false to abort the load. */
typedef bool (*recovery_image_write_fn)(void *ctx, uint32_t addr, const uint8_t *data, size_t len);

typedef struct {
    // set by recovery_image_init()
    uint32_t load_min;
    uint32_t load_max;      // exclusive
    const char *family;     // NULL accepts any .cfw family
    recovery_image_write_fn write;
    void *ctx;

    // progress
    enum { IMAGE_DETECT, IMAGE_PREHEADER, IMAGE_HEADER, IMAGE_DATA, IMAGE_PAD, IMAGE_FAILED } state;
    uint8_t header[RECOVERY_IMAGE_PREHEADER];
    size_t have;            // bytes collected in header
    uint32_t addr;          // where the next data byte goes
    uint32_t remaining;     // data bytes left in this record
    uint32_t pad;           // padding bytes left after it
    uint32_t start;
    uint32_t records;
    uint32_t loaded;        // data bytes handed to `write`
    recovery_image_result_t error;
} recovery_image_t;

/* Human readable name of a result, for logging. Never returns NULL. */
const char *recovery_image_result_name(recovery_image_result_t result);

/* Prepares to read an image whose records must all lie in [load_min, load_max). */
void recovery_image_init(recovery_image_t *im, uint32_t load_min, uint32_t load_max, const char *family,
                         recovery_image_write_fn write, void *ctx);

/* Reads the next `len` bytes of the stream. Once a call fails, every later
   call fails with RECOVERY_IMAGE_ERR_FAILED. */
recovery_image_result_t recovery_image_feed(recovery_image_t *im, const uint8_t *data, size_t len);

/* Called after the last byte; sets `start` to where the image is started. */
recovery_image_result_t recovery_image_finish(recovery_image_t *im, uint32_t *start);

#endif /* SOFTWARE_U64CTRL_MAIN_RECOVERY_IMAGE_H_ */
