/*
 * recovery_manifest.h
 *
 * The manifest a recovery server hands out: which runtime FPGA bitstream and
 * which update image to load, for which boards, signed by the publisher.
 *
 * A manifest is plain text, one key=value pair per line, LF endings:
 *
 *   format=1
 *   package=u64e2-3.14
 *   idcode=0362c093
 *   serial=*
 *   fpga.file=u64e2_50t.bit
 *   fpga.size=2192121
 *   fpga.sha256=<64 hex digits>
 *   image.file=update.ue2
 *   image.size=8123456
 *   image.sha256=<64 hex digits>
 *   sig=<base64 DER ECDSA P-256 signature>
 *
 * The signature covers every byte before the sig line, and the sig line is
 * always last. File names are relative to the manifest's own location, so a
 * signed manifest and its files can be mirrored unchanged on another server.
 * format is always the first line. Every key except serial is required, none
 * may repeat, and an unknown key is rejected: a field that changes what a
 * manifest means comes with a new format number, which older modules refuse.
 *
 * The recovery server's manifest.py writes these; both sides are tested
 * against the same signed vectors in software/test/recovery_manifest.
 *
 * Use: recovery_manifest_split() finds the signature, recovery_manifest_verify()
 * checks it, and only then does recovery_manifest_parse() read the signed text.
 * Only the split and the verify ever see unauthenticated input.
 *
 * Everything but recovery_manifest_verify() is free of ESP-IDF and mbedtls.
 */

#ifndef SOFTWARE_U64CTRL_MAIN_RECOVERY_MANIFEST_H_
#define SOFTWARE_U64CTRL_MAIN_RECOVERY_MANIFEST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECOVERY_MANIFEST_FORMAT    1
#define RECOVERY_MANIFEST_MAX_SIZE  2048
#define RECOVERY_NAME_MAX           63   // package and file names
#define RECOVERY_SERIAL_PATTERN_MAX 63
#define RECOVERY_SIG_MAX            80   // DER ECDSA P-256 is at most 72 bytes

/* The top four bits of a JTAG IDCODE are the silicon revision. A manifest
   names the device, so a newer revision of the same FPGA must still match. */
#define RECOVERY_IDCODE_MASK        0x0FFFFFFFu

typedef enum {
    RECOVERY_OK = 0,
    RECOVERY_ERR_TOO_LARGE,     // longer than RECOVERY_MANIFEST_MAX_SIZE
    RECOVERY_ERR_NO_SIGNATURE,  // no sig line, or it is not the last line
    RECOVERY_ERR_BAD_SIGNATURE, // does not verify against any trusted key
    RECOVERY_ERR_SYNTAX,        // a line that is not key=value, or a CR
    RECOVERY_ERR_FORMAT,        // a format number this module does not know
    RECOVERY_ERR_UNKNOWN_KEY,
    RECOVERY_ERR_DUPLICATE_KEY,
    RECOVERY_ERR_MISSING_KEY,
    RECOVERY_ERR_BAD_VALUE,
} recovery_result_t;

typedef struct {
    char name[RECOVERY_NAME_MAX + 1];
    uint32_t size;
    uint8_t sha256[32];
} recovery_file_t;

typedef struct {
    char package[RECOVERY_NAME_MAX + 1];
    uint32_t idcode;
    char serial[RECOVERY_SERIAL_PATTERN_MAX + 1];  // a pattern; "*" when absent
    recovery_file_t fpga;
    recovery_file_t image;
} recovery_manifest_t;

typedef struct {
    size_t signed_len;      // bytes at the start of the text the signature covers
    const char *sig_base64; // points into the text
    size_t sig_base64_len;
} recovery_signature_t;

/* Human readable name of a result, for logging. Never returns NULL. */
const char *recovery_result_name(recovery_result_t result);

/* Locates the sig line in `len` bytes of `text`. The text may end in one LF
   after the signature, and need not be NUL terminated. */
recovery_result_t recovery_manifest_split(const char *text, size_t len, recovery_signature_t *sig);

/* Checks the signature against each of the `n_keys` PEM public keys in turn.
   RECOVERY_OK when one of them verifies it. */
recovery_result_t recovery_manifest_verify(const char *text, const recovery_signature_t *sig,
                                           const char *const *keys_pem, int n_keys);

/* Reads the first `len` bytes of `text`, which must be the signed part that
   recovery_manifest_split() found. `m` is only valid when RECOVERY_OK. */
recovery_result_t recovery_manifest_parse(const char *text, size_t len, recovery_manifest_t *m);

/* Whether `serial` matches `pattern`: '*' matches any run of characters, '?'
   any one, [abc] and [a-z] one of a set, [!abc] one not in it. Anything else,
   including a '[' without a closing ']', matches itself. These are the rules of
   Python's fnmatch.fnmatchcase(), which the recovery server uses. */
bool recovery_serial_match(const char *pattern, const char *serial);

/* Whether the manifest applies to the board with this IDCODE and serial. */
bool recovery_manifest_for_board(const recovery_manifest_t *m, uint32_t idcode, const char *serial);

#endif /* SOFTWARE_U64CTRL_MAIN_RECOVERY_MANIFEST_H_ */
