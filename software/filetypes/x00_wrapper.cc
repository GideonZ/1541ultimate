#include "x00_wrapper.h"
#include <ctype.h>
#include <string.h>

bool x00_extension(const char *ext, char *type_letter)
{
    if (!ext || !ext[0] || !isdigit((uint8_t)ext[1]) || !isdigit((uint8_t)ext[2]) || ext[3]) {
        return false;
    }
    char letter = (char)toupper((uint8_t)ext[0]);
    if ((letter != 'P') && (letter != 'S') && (letter != 'U') && (letter != 'R')) {
        return false;
    }
    if (type_letter) {
        *type_letter = letter;
    }
    return true;
}

bool x00_name(const char *path, char *type_letter)
{
    const char *dot = strrchr(path, '.');
    return dot && !strchr(dot, '/') && x00_extension(dot + 1, type_letter);
}

bool x00_header(const uint8_t *header, uint32_t length, char *cbm_name, uint8_t *record_length)
{
    if ((length < X00_HEADER_SIZE) || (memcmp(header, "C64File", 8) != 0)) {
        return false;
    }
    if (cbm_name) {
        memcpy(cbm_name, header + 8, 16);
        cbm_name[16] = 0;
    }
    if (record_length) {
        *record_length = header[25];
    }
    return true;
}

uint32_t x00_skip_header(File *f, const char *path, uint8_t *record_length)
{
    uint8_t head[X00_HEADER_SIZE];
    uint32_t got = 0;
    if (!f || !x00_name(path, NULL) || !f->get_size()) {
        return 0;
    }
    if ((f->read(head, X00_HEADER_SIZE, &got) == FR_OK) && x00_header(head, got, NULL, record_length)) {
        return X00_HEADER_SIZE;
    }
    f->seek(0);
    return 0;
}
