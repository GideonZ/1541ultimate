/*
 * recovery_url.c
 *
 * See recovery_url.h.
 */

#include <stdio.h>
#include <string.h>
#include "recovery_url.h"

/* The length of the scheme and "://", or 0 for a scheme we do not speak. */
static size_t scheme_len(const char *url)
{
    if (strncmp(url, "http://", 7) == 0) {
        return 7;
    }
    if (strncmp(url, "https://", 8) == 0) {
        return 8;
    }
    return 0;
}

bool recovery_url_valid(const char *url)
{
    size_t len = strlen(url);
    if (len == 0) {
        return true;
    }
    if (len > RECOVERY_URL_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (url[i] <= 0x20 || url[i] > 0x7E) {
            return false;
        }
    }
    size_t s = scheme_len(url);
    return s && url[s] && url[s] != '/' && url[s] != '?';
}

bool recovery_request_url(char *out, size_t size, const char *base, const char *serial, uint32_t idcode,
                          int esp_major, int esp_minor)
{
    size_t len = strlen(base);
    if (len && base[len - 1] == '/') {
        len--;
    }
    int n = snprintf(out, size, "%.*s/v1/recovery?serial=", (int)len, base);
    if (n < 0 || (size_t)n >= size) {
        return false;
    }
    size_t pos = (size_t)n;
    static const char hex[] = "0123456789ABCDEF";
    for (const char *c = serial; *c; c++) {
        bool plain = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                     *c == '-' || *c == '.' || *c == '_' || *c == '~';
        if (pos + (plain ? 1 : 3) >= size) {
            return false;
        }
        if (plain) {
            out[pos++] = *c;
        } else {
            out[pos++] = '%';
            out[pos++] = hex[(*c >> 4) & 15];
            out[pos++] = hex[*c & 15];
        }
    }
    n = snprintf(out + pos, size - pos, "&idcode=%08lx&esp=%d.%d", (unsigned long)idcode, esp_major, esp_minor);
    return n >= 0 && (size_t)n < size - pos;
}

bool recovery_file_url(char *out, size_t size, const char *manifest_url, const char *name)
{
    size_t end = strcspn(manifest_url, "?#");
    size_t s = scheme_len(manifest_url);
    // The path starts at the first '/' after the host; without one, it is "/".
    const char *host = manifest_url + s;
    const char *slash = memchr(host, '/', end - s);
    size_t dir;
    const char *sep = "";
    if (!slash) {
        dir = end;
        sep = "/";
    } else {
        dir = end;
        while (manifest_url[dir - 1] != '/') {
            dir--;
        }
    }
    int n = snprintf(out, size, "%.*s%s%s", (int)dir, manifest_url, sep, name);
    return n >= 0 && (size_t)n < size;
}

bool recovery_url_resolve(char *out, size_t size, const char *base, const char *location)
{
    int n;
    if (scheme_len(location)) {
        n = snprintf(out, size, "%s", location);
    } else if (location[0] == '/' && location[1] == '/') {
        size_t s = scheme_len(base);
        n = snprintf(out, size, "%.*s%s", (int)(s ? s - 2 : 0), base, location);  // keep "scheme:"
    } else if (location[0] == '/') {
        // The origin is everything up to the first '/', '?' or '#' after the host.
        size_t s = scheme_len(base);
        size_t origin = s + strcspn(base + s, "/?#");
        n = snprintf(out, size, "%.*s%s", (int)origin, base, location);
    } else {
        return recovery_file_url(out, size, base, location);
    }
    return n >= 0 && (size_t)n < size;
}
