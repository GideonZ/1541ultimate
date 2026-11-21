/*
 * esp_image.h
 *
 * The container of a .ESP file: a series of segments, each a header of three little-endian
 * words (signature "SEGM", length, flash address) followed by that many bytes. Header only,
 * no dependencies, so that a host test can include it.
 */
#ifndef ESP_IMAGE_H_
#define ESP_IMAGE_H_

#include <stdint.h>

#define ESP_SEGMENT_SIGNATURE 0x4d474553
#define ESP_SEGMENT_HEADER    12

enum EspImageStatus {
    ESP_IMAGE_OK = 0,
    ESP_IMAGE_EMPTY,        // no segment at all
    ESP_IMAGE_BAD_HEADER,   // less than a header left
    ESP_IMAGE_BAD_SIGNATURE,
    ESP_IMAGE_TRUNCATED,    // a segment is longer than what is left of the file
    ESP_IMAGE_READ_ERROR
};

// Reads len bytes at pos; false when it cannot
typedef bool (*EspReadAt_t)(void *context, uint32_t pos, void *buffer, uint32_t len);

// Walks the whole container, by its headers, and checks it ends exactly where the file does
static inline EspImageStatus esp_image_check(uint32_t filesize, EspReadAt_t read_at, void *context)
{
    uint32_t pos = 0;
    uint32_t header[3];
    if (filesize == 0) {
        return ESP_IMAGE_EMPTY;
    }
    while (pos < filesize) {
        if (filesize - pos < ESP_SEGMENT_HEADER) {
            return ESP_IMAGE_BAD_HEADER;
        }
        if (!read_at(context, pos, header, ESP_SEGMENT_HEADER)) {
            return ESP_IMAGE_READ_ERROR;
        }
        if (header[0] != ESP_SEGMENT_SIGNATURE) {
            return ESP_IMAGE_BAD_SIGNATURE;
        }
        pos += ESP_SEGMENT_HEADER;
        if (header[1] > filesize - pos) {
            return ESP_IMAGE_TRUNCATED;
        }
        pos += header[1];
    }
    return ESP_IMAGE_OK;
}

static inline const char *esp_image_message(EspImageStatus s)
{
    switch (s) {
    case ESP_IMAGE_EMPTY:         return "File is empty.";
    case ESP_IMAGE_BAD_HEADER:    return "Could not read header.";
    case ESP_IMAGE_BAD_SIGNATURE: return "Incorrect signature in file.";
    case ESP_IMAGE_TRUNCATED:     return "File is truncated.";
    case ESP_IMAGE_READ_ERROR:    return "Could not read file.";
    default:                      return "";
    }
}

#endif /* ESP_IMAGE_H_ */
