#include "x00_wrapper.h"
#include "filemanager.h"
#include "pattern.h"
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

bool x00_read_header(FileManager *fm, const char *path, char *cbm_name, uint8_t *record_length)
{
    File *f = NULL;
    if (!x00_name(path, NULL) || (fm->fopen(path, FA_READ, &f) != FR_OK)) {
        return false;
    }
    uint8_t head[X00_HEADER_SIZE];
    uint32_t got = 0;
    bool found = (f->read(head, X00_HEADER_SIZE, &got) == FR_OK) &&
                 x00_header(head, got, cbm_name, record_length);
    fm->fclose(f);
    return found;
}

int x00_shown_name(char *cbm_name)
{
    int len = 0;
    while ((len < 16) && ((uint8_t)cbm_name[len] >= 0x20) && ((uint8_t)cbm_name[len] != 0xA0)) {
        len++;
    }
    cbm_name[len] = 0;
    return len;
}

uint32_t x00_skip_header(File *f, const char *path, uint8_t *record_length, char *cbm_name)
{
    uint8_t head[X00_HEADER_SIZE];
    uint32_t got = 0;
    if (!f || !x00_name(path, NULL) || !f->get_size()) {
        return 0;
    }
    if ((f->read(head, X00_HEADER_SIZE, &got) == FR_OK) &&
        x00_header(head, got, cbm_name, record_length)) {
        return X00_HEADER_SIZE;
    }
    f->seek(0);
    return 0;
}

// One candidate host name for a CBM name: the name rendered for the file system, the type
// letter of the wrapper at `path`, and the two digits of `index`.
static bool x00_host_name(const char *path, const char *dir, const char *cbm_name, int index,
                          mstring& out)
{
    char letter = 0;
    char fatname[52];
    if (!x00_name(path, &letter)) {
        return false;
    }
    petscii_to_fat(cbm_name, fatname, sizeof(fatname));
    if (!fatname[0]) {
        return false;
    }
    char tail[5] = { '.', letter, (char)('0' + (index / 10)), (char)('0' + (index % 10)), 0 };
    out = dir;
    if (out[-1] != '/') {
        out += "/";
    }
    out += fatname;
    out += tail;
    return true;
}

FRESULT x00_rename(FileManager *fm, const char *path, const char *dir, const char *cbm_name,
                   mstring *renamed)
{
    File *f = NULL;
    FRESULT fres = fm->fopen(path, FA_READ | FA_WRITE, &f);
    if (fres != FR_OK) {
        return fres;
    }
    char name[16];
    uint32_t written;
    memset(name, 0, sizeof(name));
    strncpy(name, cbm_name, sizeof(name));
    fres = f->seek(8);
    if (fres == FR_OK) {
        fres = f->write(name, sizeof(name), &written);
    }
    fm->fclose(f);
    if (fres != FR_OK) {
        return fres;
    }
    mstring target(path);
    for (int i = 0; i < 100; i++) {
        mstring candidate;
        FileInfo info(INFO_SIZE);
        if (!x00_host_name(path, dir, cbm_name, i, candidate)) {
            break; // no host name can be built, so the file keeps the one it has
        }
        if (!strcasecmp(candidate.c_str(), path)) {
            break; // the name it has; the file system matches names without regard to case
        }
        if (fm->fstat(candidate.c_str(), info) != FR_OK) {
            target = candidate;
            break;
        }
    }
    if (strcmp(target.c_str(), path)) {
        fres = fm->rename(path, target.c_str());
    }
    if (renamed && (fres == FR_OK)) {
        *renamed = target;
    }
    return fres;
}
