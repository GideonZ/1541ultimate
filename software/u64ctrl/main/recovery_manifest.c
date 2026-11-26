/*
 * recovery_manifest.c
 *
 * See recovery_manifest.h. The signature check lives in recovery_verify.c, so
 * this file builds on the host without mbedtls.
 */

#include <string.h>
#include "recovery_manifest.h"

/* Base64 of the largest signature, rounded up to whole groups of four. */
#define SIG_BASE64_MAX (((RECOVERY_SIG_MAX + 2) / 3) * 4)

const char *recovery_result_name(recovery_result_t result)
{
    switch (result) {
        case RECOVERY_OK:                return "ok";
        case RECOVERY_ERR_TOO_LARGE:     return "manifest too large";
        case RECOVERY_ERR_NO_SIGNATURE:  return "no signature";
        case RECOVERY_ERR_BAD_SIGNATURE: return "bad signature";
        case RECOVERY_ERR_SYNTAX:        return "syntax error";
        case RECOVERY_ERR_FORMAT:        return "unsupported format";
        case RECOVERY_ERR_UNKNOWN_KEY:   return "unknown key";
        case RECOVERY_ERR_DUPLICATE_KEY: return "duplicate key";
        case RECOVERY_ERR_MISSING_KEY:   return "missing key";
        case RECOVERY_ERR_BAD_VALUE:     return "bad value";
    }
    return "unknown result";
}

recovery_result_t recovery_manifest_split(const char *text, size_t len, recovery_signature_t *sig)
{
    if (len > RECOVERY_MANIFEST_MAX_SIZE) {
        return RECOVERY_ERR_TOO_LARGE;
    }
    size_t end = len;
    if (end && text[end - 1] == '\n') {
        end--;
    }
    size_t line = end;
    while (line && text[line - 1] != '\n') {
        line--;
    }
    // A signature with nothing before it signs nothing.
    if (line == 0 || (end - line) <= 4 || memcmp(text + line, "sig=", 4) != 0) {
        return RECOVERY_ERR_NO_SIGNATURE;
    }
    size_t b64_len = end - line - 4;
    if (b64_len > SIG_BASE64_MAX) {
        return RECOVERY_ERR_NO_SIGNATURE;
    }
    sig->signed_len = line;
    sig->sig_base64 = text + line + 4;
    sig->sig_base64_len = b64_len;
    return RECOVERY_OK;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex_bytes(const char *v, size_t len, uint8_t *out, size_t bytes)
{
    if (len != 2 * bytes) {
        return false;
    }
    for (size_t i = 0; i < bytes; i++) {
        int hi = hex_digit(v[2 * i]);
        int lo = hex_digit(v[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool parse_idcode(const char *v, size_t len, uint32_t *out)
{
    uint8_t b[4];
    if (!parse_hex_bytes(v, len, b, 4)) {
        return false;
    }
    *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    return true;
}

static bool parse_size(const char *v, size_t len, uint32_t *out)
{
    if (len == 0 || len > 10) {
        return false;
    }
    uint64_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (v[i] < '0' || v[i] > '9') {
            return false;
        }
        n = n * 10 + (uint64_t)(v[i] - '0');
    }
    if (n == 0 || n > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)n;
    return true;
}

static bool name_char(char c, bool allow_slash)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-' || (allow_slash && c == '/');
}

static bool copy_name(char *dst, const char *v, size_t len)
{
    if (len == 0 || len > RECOVERY_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!name_char(v[i], false)) {
            return false;
        }
    }
    memcpy(dst, v, len);
    dst[len] = 0;
    return true;
}

/* A path below the manifest's own location: no leading '/', and no empty,
   "." or ".." segment that would step outside it. */
static bool copy_file_name(char *dst, const char *v, size_t len)
{
    if (len == 0 || len > RECOVERY_NAME_MAX) {
        return false;
    }
    size_t seg = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || v[i] == '/') {
            size_t n = i - seg;
            if (n == 0 || (n == 1 && v[seg] == '.') || (n == 2 && v[seg] == '.' && v[seg + 1] == '.')) {
                return false;
            }
            seg = i + 1;
        } else if (!name_char(v[i], false)) {
            return false;
        }
    }
    memcpy(dst, v, len);
    dst[len] = 0;
    return true;
}

typedef enum {
    KEY_FORMAT, KEY_PACKAGE, KEY_IDCODE, KEY_SERIAL,
    KEY_FPGA_FILE, KEY_FPGA_SIZE, KEY_FPGA_SHA256,
    KEY_IMAGE_FILE, KEY_IMAGE_SIZE, KEY_IMAGE_SHA256,
    KEY_COUNT
} manifest_key_t;

static const char *key_names[KEY_COUNT] = {
    "format", "package", "idcode", "serial",
    "fpga.file", "fpga.size", "fpga.sha256",
    "image.file", "image.size", "image.sha256",
};

#define REQUIRED_KEYS (((1u << KEY_COUNT) - 1) & ~(1u << KEY_SERIAL))

static bool parse_value(recovery_manifest_t *m, manifest_key_t key, const char *v, size_t len)
{
    switch (key) {
        case KEY_PACKAGE:      return copy_name(m->package, v, len);
        case KEY_IDCODE:       return parse_idcode(v, len, &m->idcode);
        case KEY_FPGA_FILE:    return copy_file_name(m->fpga.name, v, len);
        case KEY_FPGA_SIZE:    return parse_size(v, len, &m->fpga.size);
        case KEY_FPGA_SHA256:  return parse_hex_bytes(v, len, m->fpga.sha256, 32);
        case KEY_IMAGE_FILE:   return copy_file_name(m->image.name, v, len);
        case KEY_IMAGE_SIZE:   return parse_size(v, len, &m->image.size);
        case KEY_IMAGE_SHA256: return parse_hex_bytes(v, len, m->image.sha256, 32);
        case KEY_SERIAL:
            if (len == 0 || len > RECOVERY_SERIAL_PATTERN_MAX) {
                return false;
            }
            memcpy(m->serial, v, len);
            m->serial[len] = 0;
            return true;
        default:
            return false;
    }
}

recovery_result_t recovery_manifest_parse(const char *text, size_t len, recovery_manifest_t *m)
{
    memset(m, 0, sizeof(*m));
    uint32_t seen = 0;
    size_t pos = 0;

    while (pos < len) {
        size_t eol = pos;
        while (eol < len && text[eol] != '\n') {
            if (text[eol] < 0x20 || text[eol] > 0x7E) {
                return RECOVERY_ERR_SYNTAX;  // CR, NUL and other control bytes
            }
            eol++;
        }
        if (eol == len) {
            return RECOVERY_ERR_SYNTAX;  // the signed part always ends in LF
        }
        const char *eq = memchr(text + pos, '=', eol - pos);
        if (!eq || eq == text + pos) {
            return RECOVERY_ERR_SYNTAX;
        }
        size_t key_len = (size_t)(eq - (text + pos));
        const char *v = eq + 1;
        size_t v_len = (size_t)((text + eol) - v);

        int key = -1;
        for (int k = 0; k < KEY_COUNT; k++) {
            if (strlen(key_names[k]) == key_len && memcmp(key_names[k], text + pos, key_len) == 0) {
                key = k;
                break;
            }
        }
        if (pos == 0) {
            if (key != KEY_FORMAT || v_len != 1 || v[0] != '0' + RECOVERY_MANIFEST_FORMAT) {
                return RECOVERY_ERR_FORMAT;
            }
        } else if (key < 0) {
            return RECOVERY_ERR_UNKNOWN_KEY;
        } else if (seen & (1u << key)) {
            return RECOVERY_ERR_DUPLICATE_KEY;
        } else if (!parse_value(m, (manifest_key_t)key, v, v_len)) {
            return RECOVERY_ERR_BAD_VALUE;
        }
        seen |= 1u << key;
        pos = eol + 1;
    }

    if (len == 0) {
        return RECOVERY_ERR_FORMAT;
    }
    if ((seen & REQUIRED_KEYS) != REQUIRED_KEYS) {
        return RECOVERY_ERR_MISSING_KEY;
    }
    if (!(seen & (1u << KEY_SERIAL))) {
        strcpy(m->serial, "*");
    }
    return RECOVERY_OK;
}

/* Matches `c` against the set that opens at `p`, a '['. Returns -1 when the
   '[' has no closing ']' and so stands for itself. Otherwise sets `next` past
   the ']' and returns whether `c` is in the set. Like fnmatch, a ']' directly
   after the '[' or "[!" is a member, not the end. */
static int match_set(const char *p, char c, const char **next)
{
    const char *q = p + 1;
    bool negate = (*q == '!');
    if (negate) {
        q++;
    }
    const char *first = q;
    if (*q == ']') {
        q++;
    }
    while (*q && *q != ']') {
        q++;
    }
    if (!*q) {
        return -1;
    }
    *next = q + 1;

    bool found = false;
    const unsigned char uc = (unsigned char)c;
    for (const char *k = first; k < q; ) {
        if (k[1] == '-' && k + 2 < q) {
            if (uc >= (unsigned char)k[0] && uc <= (unsigned char)k[2]) {
                found = true;
            }
            k += 3;
        } else {
            if (uc == (unsigned char)*k) {
                found = true;
            }
            k++;
        }
    }
    return found != negate;
}

bool recovery_serial_match(const char *pattern, const char *serial)
{
    const char *p = pattern;
    const char *s = serial;
    const char *star_p = NULL;  // the pattern just after the last '*'
    const char *star_s = NULL;  // where that '*' started matching

    while (*s) {
        if (*p == '*') {
            star_p = ++p;
            star_s = s;
            continue;
        }
        if (*p) {
            const char *next = p + 1;
            int m;
            if (*p == '?') {
                m = 1;
            } else if (*p == '[' && (m = match_set(p, *s, &next)) >= 0) {
                // m and next set by match_set
            } else {
                m = (*p == *s);
                next = p + 1;
            }
            if (m) {
                p = next;
                s++;
                continue;
            }
        }
        if (!star_p) {
            return false;
        }
        // Let the last '*' swallow one more character and try again.
        p = star_p;
        s = ++star_s;
    }
    while (*p == '*') {
        p++;
    }
    return *p == 0;
}

bool recovery_manifest_for_board(const recovery_manifest_t *m, uint32_t idcode, const char *serial)
{
    return ((m->idcode ^ idcode) & RECOVERY_IDCODE_MASK) == 0 &&
           recovery_serial_match(m->serial, serial);
}
