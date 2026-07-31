#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "screen.h"

static const int MATRIX_WIDTH = 3;
static const int MATRIX_HEIGHT = 2;
static const int MATRIX_CELLS = MATRIX_WIDTH * MATRIX_HEIGHT;

extern "C" void outbyte(int b)
{
    char c = (char)b;
    fwrite(&c, 1, 1, stdout);
}

static int fail(const char *message)
{
    puts(message);
    return 1;
}

static int expect_bytes(const char *label, const uint8_t *actual, const uint8_t *expected, int len)
{
    if (memcmp(actual, expected, len) == 0) {
        return 0;
    }
    printf("Unexpected %s bytes.\n", label);
    for (int i = 0; i < len; i++) {
        printf("%02X%s", actual[i], (i == len - 1) ? "\n" : " ");
    }
    return 1;
}

int main()
{
    uint8_t chars[MATRIX_CELLS] = { 0x01, 0x82, 0x03, 0x44, 0x05, 0xC6 };
    uint8_t colors[MATRIX_CELLS] = { 0x10, 0x21, 0x32, 0x43, 0xF4, 0x8F };
    uint8_t initial_cell_colour_codes[MATRIX_CELLS] = { 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F };
    uint8_t out_chars[MATRIX_CELLS] = { 0 };
    uint8_t out_colors[MATRIX_CELLS] = { 0 };
    int width = 0;
    int height = 0;

    Screen_MemMappedCharMatrix screen((char *)(void *)chars, (char *)(void *)colors, MATRIX_WIDTH, MATRIX_HEIGHT);
    if (!screen.copy_matrix(out_chars, out_colors, MATRIX_CELLS, &width, &height)) {
        return fail("copy_matrix unexpectedly failed.");
    }
    if ((width != MATRIX_WIDTH) || (height != MATRIX_HEIGHT)) {
        return fail("Unexpected matrix dimensions.");
    }
    if (expect_bytes("character", out_chars, chars, MATRIX_CELLS)) {
        return 1;
    }
    if (expect_bytes("initial cell colour code", out_colors, initial_cell_colour_codes, MATRIX_CELLS)) {
        return 1;
    }

    screen.move_cursor(0, 0);
    screen.set_color(1);
    screen.set_background(6);
    screen.output('X');
    colors[0] &= 0x0F;
    memset(out_colors, 0, sizeof(out_colors));
    if (!screen.copy_matrix(out_chars, out_colors, MATRIX_CELLS, &width, &height)) {
        return fail("copy_matrix failed after writing a coloured cell.");
    }
    if (out_colors[0] != 0x61) {
        printf("Unexpected cell colour code byte: %02X\n", out_colors[0]);
        return 1;
    }

    screen.clear();
    screen.move_cursor(0, MATRIX_HEIGHT - 1);
    screen.set_color(1);
    screen.set_background(6);
    screen.output('x');
    screen.output('y');
    screen.output('\n');
    memset(out_colors, 0, sizeof(out_colors));
    if (!screen.copy_matrix(out_chars, out_colors, MATRIX_CELLS, &width, &height)) {
        return fail("copy_matrix failed after scrolling.");
    }
    for (int i = MATRIX_WIDTH; i < MATRIX_CELLS; i++) {
        if (out_colors[i] != 0x0F) {
            printf("Unexpected blank row colour code byte at %d: %02X\n", i, out_colors[i]);
            return 1;
        }
    }

    width = 0;
    height = 0;
    if (screen.copy_matrix(out_chars, out_colors, MATRIX_CELLS - 1, &width, &height)) {
        return fail("copy_matrix succeeded with an undersized destination.");
    }
    if ((width != MATRIX_WIDTH) || (height != MATRIX_HEIGHT)) {
        return fail("Undersized copy did not report matrix dimensions.");
    }

    puts("screen_snapshot_test: OK");
    return 0;
}
