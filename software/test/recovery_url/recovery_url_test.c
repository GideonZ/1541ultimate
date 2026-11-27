/*
 * Host side tests for the recovery URL helpers of the control module.
 *
 *   make -C target/pc/linux/recoveryurl test
 *
 * The real software/u64ctrl/main/recovery_url.c is compiled here.
 */
#include <stdio.h>
#include <string.h>
#include "recovery_url.h"

static int failures;
static int checks;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

static void check_str(bool ok, const char *got, const char *expected, const char *what)
{
    checks++;
    if (!ok || strcmp(got, expected) != 0) {
        failures++;
        printf("  FAIL  %s\n        expected '%s'\n        got      '%s'%s\n", what, expected, got, ok ? "" : " (refused)");
    } else {
        printf("  ok    %s\n", what);
    }
}

int main(void)
{
    char out[256];
    char long_url[200];

    printf("Server URLs\n");
    check(recovery_url_valid(""), "none at all");
    check(recovery_url_valid("https://recovery.example.org"), "https with a host");
    check(recovery_url_valid("http://192.168.1.10:8080/"), "http on the LAN, with a port");
    check(recovery_url_valid("https://example.org/ultimate/recovery"), "with a path");
    check(!recovery_url_valid("ftp://example.org"), "not another scheme");
    check(!recovery_url_valid("example.org"), "not without a scheme");
    check(!recovery_url_valid("https://"), "not without a host");
    check(!recovery_url_valid("https:///path"), "not with an empty host");
    check(!recovery_url_valid("https://example.org/a b"), "not with a space");
    memset(long_url, 'a', sizeof(long_url));
    memcpy(long_url, "https://", 8);
    long_url[RECOVERY_URL_MAX] = 0;
    check(recovery_url_valid(long_url), "of the longest length");
    long_url[RECOVERY_URL_MAX] = 'a';
    long_url[RECOVERY_URL_MAX + 1] = 0;
    check(!recovery_url_valid(long_url), "not one longer");

    printf("The request\n");
    check_str(recovery_request_url(out, sizeof(out), "https://recovery.example.org", "24051234", 0x0362c093, 1, 15),
              out, "https://recovery.example.org/v1/recovery?serial=24051234&idcode=0362c093&esp=1.15", "for a server");
    check_str(recovery_request_url(out, sizeof(out), "http://10.0.0.2:8080/", "NOSERIAL", 0x13631093, 1, 15),
              out, "http://10.0.0.2:8080/v1/recovery?serial=NOSERIAL&idcode=13631093&esp=1.15",
              "drops a trailing slash of the base");
    check_str(recovery_request_url(out, sizeof(out), "https://a.org/r", "C64U #1&2", 0x0362c093, 1, 15),
              out, "https://a.org/r/v1/recovery?serial=C64U%20%231%262&idcode=0362c093&esp=1.15",
              "encodes what is not plain in a serial");
    check(!recovery_request_url(out, 40, "https://recovery.example.org", "24051234", 0x0362c093, 1, 15),
          "refuses what does not fit");
    check(!recovery_request_url(out, 50, "https://recovery.example.org", "24051234", 0x0362c093, 1, 15),
          "refuses a serial that does not fit");

    printf("Files next to the manifest\n");
    check_str(recovery_file_url(out, sizeof(out), "https://a.org/packages/u64e2-3.15/manifest", "update.cfw"),
              out, "https://a.org/packages/u64e2-3.15/update.cfw", "replace its last segment");
    check_str(recovery_file_url(out, sizeof(out), "https://a.org/packages/p/manifest?x=1#f", "bits/a.bit"),
              out, "https://a.org/packages/p/bits/a.bit", "drop its query and fragment");
    check_str(recovery_file_url(out, sizeof(out), "http://10.0.0.2:8080/manifest", "a.bit"),
              out, "http://10.0.0.2:8080/a.bit", "at the root");
    check_str(recovery_file_url(out, sizeof(out), "http://10.0.0.2:8080", "a.bit"),
              out, "http://10.0.0.2:8080/a.bit", "when the URL has no path");
    check_str(recovery_file_url(out, sizeof(out), "http://10.0.0.2:8080?q", "a.bit"),
              out, "http://10.0.0.2:8080/a.bit", "when the URL has only a query");
    check(!recovery_file_url(out, 20, "https://a.org/packages/p/manifest", "update.cfw"), "refuse what does not fit");

    printf("Following a redirect\n");
    const char *req = "http://172.16.52.55:8080/v1/recovery?serial=1&idcode=0362c093&esp=1.16";
    check_str(recovery_url_resolve(out, sizeof(out), req, "/packages/p/manifest"),
              out, "http://172.16.52.55:8080/packages/p/manifest", "to a path keeps the scheme, host and port");
    check_str(recovery_url_resolve(out, sizeof(out), "https://a.org?x", "/m"), out, "https://a.org/m",
              "to a path from a URL with only a query");
    check_str(recovery_url_resolve(out, sizeof(out), req, "https://mirror.org/p/manifest"),
              out, "https://mirror.org/p/manifest", "to an absolute URL goes there");
    check_str(recovery_url_resolve(out, sizeof(out), req, "//mirror.org:81/p/manifest"),
              out, "http://mirror.org:81/p/manifest", "to another host keeps only the scheme");
    check_str(recovery_url_resolve(out, sizeof(out), "http://h:8080/a/b?q", "c/manifest"),
              out, "http://h:8080/a/c/manifest", "to a relative path lands next to the last segment");
    check(!recovery_url_resolve(out, 30, req, "/packages/p/manifest"), "refuses what does not fit");

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
