/*
 * recovery_url.h
 *
 * The URLs of a recovery: which server URLs are acceptable, the request a
 * board sends, and where the files a manifest names are found.
 *
 * Free of ESP-IDF, so software/test/recovery_url tests what ships.
 */

#ifndef SOFTWARE_U64CTRL_MAIN_RECOVERY_URL_H_
#define SOFTWARE_U64CTRL_MAIN_RECOVERY_URL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECOVERY_URL_MAX 127  // a server URL as stored, without the NUL

/* An http:// or https:// URL with a host, at most RECOVERY_URL_MAX printable
   characters and no spaces. The empty string is valid too: no server. */
bool recovery_url_valid(const char *url);

/* <base>/v1/recovery?serial=...&idcode=xxxxxxxx&esp=major.minor, with the
   serial percent-encoded and a trailing '/' of `base` dropped. False when it
   does not fit in `size` bytes. */
bool recovery_request_url(char *out, size_t size, const char *base, const char *serial, uint32_t idcode,
                          int esp_major, int esp_minor);

/* The URL of `name` next to the manifest at `manifest_url`: its query is
   dropped and `name` replaces the last segment of its path. False when it
   does not fit in `size` bytes. */
bool recovery_file_url(char *out, size_t size, const char *manifest_url, const char *name);

/* Where a redirect to `location` leads from `base`: an absolute URL as it is,
   "//host/..." with the scheme of `base`, "/path" on the scheme, host and
   port of `base`, and anything else next to the last segment of its path.
   False when it does not fit in `size` bytes. */
bool recovery_url_resolve(char *out, size_t size, const char *base, const char *location);

#endif /* SOFTWARE_U64CTRL_MAIN_RECOVERY_URL_H_ */
