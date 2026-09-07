#include "palette_command.h"

#include <assert.h>
#include <string.h>

int main()
{
    uint8_t message[UCI_PALETTE_BYTES + 2];
    for (int i = 0; i < UCI_PALETTE_BYTES; ++i) {
        message[i + 2] = (uint8_t)(i * 5);
    }

    uint8_t palette[UCI_PALETTE_COLORS][3];
    memset(palette, 0xA5, sizeof(palette));
    assert(decode_palette_set(message, sizeof(message), palette));
    assert(!memcmp(palette, message + 2, sizeof(palette)));

    uint8_t unchanged[sizeof(palette)];
    memcpy(unchanged, palette, sizeof(palette));
    assert(!decode_palette_set(message, sizeof(message) - 1, palette));
    assert(!memcmp(palette, unchanged, sizeof(palette)));
    assert(!decode_palette_set(message, sizeof(message) + 1, palette));
    assert(!decode_palette_set(NULL, sizeof(message), palette));

    uint8_t color_message[] = { 4, 0x53, 15, 0x12, 0x34, 0x56 };
    uint8_t index = 0;
    uint8_t color[3] = { 0, 0, 0 };
    assert(decode_palette_color_set(color_message, sizeof(color_message), &index, color));
    assert(index == 15);
    assert(color[0] == 0x12 && color[1] == 0x34 && color[2] == 0x56);

    color_message[2] = UCI_PALETTE_COLORS;
    index = 9;
    memset(color, 0xA5, sizeof(color));
    assert(!decode_palette_color_set(color_message, sizeof(color_message), &index, color));
    assert(index == 9 && color[0] == 0xA5 && color[1] == 0xA5 && color[2] == 0xA5);
    assert(!decode_palette_color_set(color_message, sizeof(color_message) - 1, &index, color));
    assert(!decode_palette_color_set(NULL, sizeof(color_message), &index, color));

    assert(valid_palette_reset(2));
    assert(!valid_palette_reset(1));
    assert(!valid_palette_reset(3));
}
