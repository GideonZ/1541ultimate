/*
 * recovery_image.c
 *
 * See recovery_image.h.
 */

#include <string.h>
#include "recovery_image.h"

const char *recovery_image_result_name(recovery_image_result_t result)
{
    switch (result) {
        case RECOVERY_IMAGE_OK:             return "ok";
        case RECOVERY_IMAGE_ERR_FAMILY:     return "wrong product family";
        case RECOVERY_IMAGE_ERR_RANGE:      return "outside the load window";
        case RECOVERY_IMAGE_ERR_WRITE:      return "write failed";
        case RECOVERY_IMAGE_ERR_TRUNCATED:  return "truncated";
        case RECOVERY_IMAGE_ERR_NO_RECORDS: return "no records";
        case RECOVERY_IMAGE_ERR_NO_START:   return "no start address";
        case RECOVERY_IMAGE_ERR_FAILED:     return "already failed";
    }
    return "unknown result";
}

void recovery_image_init(recovery_image_t *im, uint32_t load_min, uint32_t load_max, const char *family,
                         recovery_image_write_fn write, void *ctx)
{
    memset(im, 0, sizeof(*im));
    im->load_min = load_min;
    im->load_max = load_max;
    im->family = family;
    im->write = write;
    im->ctx = ctx;
    im->state = IMAGE_DETECT;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static recovery_image_result_t fail(recovery_image_t *im, recovery_image_result_t error)
{
    im->state = IMAGE_FAILED;
    im->error = error;
    return error;
}

static bool in_window(const recovery_image_t *im, uint32_t addr, uint32_t length)
{
    return addr >= im->load_min && addr < im->load_max && length <= im->load_max - addr;
}

/* Collects header bytes up to `need`; true once they are all there. */
static bool collect(recovery_image_t *im, size_t need, const uint8_t **data, size_t *len)
{
    size_t n = need - im->have;
    if (n > *len) {
        n = *len;
    }
    memcpy(im->header + im->have, *data, n);
    im->have += n;
    *data += n;
    *len -= n;
    return im->have == need;
}

static recovery_image_result_t start_record(recovery_image_t *im)
{
    uint32_t load = le32(im->header);
    uint32_t length = le32(im->header + 4);
    uint32_t start = le32(im->header + 8);
    im->have = 0;
    if (length && !in_window(im, load, length)) {
        return fail(im, RECOVERY_IMAGE_ERR_RANGE);
    }
    if (start) {
        im->start = start;
    }
    im->records++;
    im->addr = load;
    im->remaining = length;
    im->pad = (4 - (length & 3)) & 3;  // hex2bin pads the data, not the length
    im->state = length ? IMAGE_DATA : IMAGE_HEADER;
    return RECOVERY_IMAGE_OK;
}

recovery_image_result_t recovery_image_feed(recovery_image_t *im, const uint8_t *data, size_t len)
{
    while (len) {
        size_t n;
        switch (im->state) {
            case IMAGE_DETECT:
                if (collect(im, 4, &data, &len)) {
                    im->state = (le32(im->header) == RECOVERY_IMAGE_FINGERPRINT) ? IMAGE_PREHEADER : IMAGE_HEADER;
                }
                break;

            case IMAGE_PREHEADER:
                if (collect(im, RECOVERY_IMAGE_PREHEADER, &data, &len)) {
                    // Like the application's loader: an empty family fits every product.
                    const char *family = (const char *)im->header + 4;
                    if (im->family && family[0] && strncmp(family, im->family, 16) != 0) {
                        return fail(im, RECOVERY_IMAGE_ERR_FAMILY);
                    }
                    im->have = 0;
                    im->state = IMAGE_HEADER;
                }
                break;

            case IMAGE_HEADER:
                if (collect(im, RECOVERY_IMAGE_RECORD_HDR, &data, &len) && start_record(im) != RECOVERY_IMAGE_OK) {
                    return im->error;
                }
                break;

            case IMAGE_DATA:
                n = (len < im->remaining) ? len : im->remaining;
                if (!im->write(im->ctx, im->addr, data, n)) {
                    return fail(im, RECOVERY_IMAGE_ERR_WRITE);
                }
                im->addr += n;
                im->remaining -= n;
                im->loaded += n;
                data += n;
                len -= n;
                if (!im->remaining) {
                    im->state = im->pad ? IMAGE_PAD : IMAGE_HEADER;
                }
                break;

            case IMAGE_PAD:
                n = (len < im->pad) ? len : im->pad;
                im->pad -= n;
                data += n;
                len -= n;
                if (!im->pad) {
                    im->state = IMAGE_HEADER;
                }
                break;

            case IMAGE_FAILED:
                return RECOVERY_IMAGE_ERR_FAILED;
        }
    }
    return (im->state == IMAGE_FAILED) ? RECOVERY_IMAGE_ERR_FAILED : RECOVERY_IMAGE_OK;
}

recovery_image_result_t recovery_image_finish(recovery_image_t *im, uint32_t *start)
{
    if (im->state == IMAGE_FAILED) {
        return RECOVERY_IMAGE_ERR_FAILED;
    }
    if (im->state == IMAGE_DETECT && im->have == 0) {
        return RECOVERY_IMAGE_ERR_NO_RECORDS;
    }
    if (im->state != IMAGE_HEADER || im->have != 0) {
        return RECOVERY_IMAGE_ERR_TRUNCATED;
    }
    if (!im->records) {
        return RECOVERY_IMAGE_ERR_NO_RECORDS;
    }
    if (!im->start) {
        return RECOVERY_IMAGE_ERR_NO_START;
    }
    if (!in_window(im, im->start, 0)) {
        return RECOVERY_IMAGE_ERR_RANGE;
    }
    *start = im->start;
    return RECOVERY_IMAGE_OK;
}
