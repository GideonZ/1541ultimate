#ifndef PALETTE_COMMAND_H
#define PALETTE_COMMAND_H

#include <stdint.h>
#include <string.h>

#define UCI_PALETTE_COLORS 16
#define UCI_PALETTE_BYTES (UCI_PALETTE_COLORS * 3)

static inline bool decode_palette_set(const uint8_t *message, int length,
                                      uint8_t rgb[UCI_PALETTE_COLORS][3])
{
    if (!message || length != UCI_PALETTE_BYTES + 2) {
        return false;
    }
    memcpy(rgb, message + 2, UCI_PALETTE_BYTES);
    return true;
}

static inline bool decode_palette_color_set(const uint8_t *message, int length,
                                            uint8_t *index, uint8_t rgb[3])
{
    if (!message || length != 6 || message[2] >= UCI_PALETTE_COLORS) {
        return false;
    }
    *index = message[2];
    memcpy(rgb, message + 3, 3);
    return true;
}

static inline bool valid_palette_reset(int length)
{
    return length == 2;
}

#endif
