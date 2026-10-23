/*
 * Host side tests for the recovery manifest reader of the control module.
 *
 *   make -C target/pc/linux/recoverymanifest test
 *
 * The real software/u64ctrl/main/recovery_manifest.c and recovery_verify.c are
 * compiled here, against the mbedtls that ESP-IDF ships, so what is exercised
 * is the code that runs on the board. The signed manifests and the serial
 * patterns in vectors/ were written by the recovery server's manifest.py and
 * Python's fnmatch; see make_vectors.py.
 *
 * The library is linked with --wrap=psa_crypto_init, --wrap=psa_hash_update and
 * --wrap=psa_hash_finish, so a test can make the crypto library's start-up or a
 * hash fail; see the __wrap_ functions below.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/pk.h"
#include "psa/crypto.h"
#include "recovery_manifest.h"
#include "recovery_verify.h"

#ifndef VECTORS_DIR
#define VECTORS_DIR "vectors"
#endif

/* recovery_trust.c, the keys the board ships with. */
extern const char *const recovery_trusted_keys[];

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

static void check_result(recovery_result_t got, recovery_result_t expected, const char *what)
{
    checks++;
    if (got != expected) {
        failures++;
        printf("  FAIL  %s (expected '%s', got '%s')\n", what, recovery_result_name(expected), recovery_result_name(got));
    } else {
        printf("  ok    %s\n", what);
    }
}

static char *read_file(const char *name, size_t *len)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", VECTORS_DIR, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        printf("cannot read %s\n", path);
        exit(2);
    }
    buf[n] = 0;
    fclose(f);
    if (len) {
        *len = (size_t)n;
    }
    return buf;
}

/* Split, verify and parse, the way the recovery task will. */
static recovery_result_t load(const char *text, size_t len, const char *const *keys, int n_keys,
                              recovery_manifest_t *m)
{
    recovery_signature_t sig;
    recovery_result_t r = recovery_manifest_split(text, len, &sig);
    if (r == RECOVERY_OK) {
        r = recovery_manifest_verify(text, &sig, keys, n_keys);
    }
    if (r == RECOVERY_OK) {
        r = recovery_manifest_parse(text, sig.signed_len, m);
    }
    return r;
}

#define HEX64 "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"

static const char *body =
    "format=1\n"
    "package=p\n"
    "idcode=0362c093\n"
    "fpga.file=a.bit\n"
    "fpga.size=1\n"
    "fpga.sha256=" HEX64 "\n"
    "image.file=b.ue2\n"
    "image.size=2\n"
    "image.sha256=" HEX64 "\n";

/* `body` with the line for `key` replaced by `line`, which may be empty to
   drop it, or hold several lines. */
static const char *with_line(const char *key, const char *line)
{
    static char out[1024];
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%s=", key);
    const char *at = strstr(body, prefix);
    if (at != body) {
        snprintf(prefix, sizeof(prefix), "\n%s=", key);
        at = strstr(body, prefix) + 1;
    }
    const char *eol = strchr(at, '\n') + 1;
    snprintf(out, sizeof(out), "%.*s%s%s", (int)(at - body), body, line, eol);
    return out;
}

static recovery_result_t parse_str(const char *text)
{
    recovery_manifest_t m;
    return recovery_manifest_parse(text, strlen(text), &m);
}

static void test_signed_vectors(void)
{
    size_t len;
    char *text = read_file("valid.manifest", &len);
    char *test_key = read_file("test_key.pub.pem", NULL);
    char *other_key = read_file("other_key.pub.pem", NULL);
    const char *trusted[] = { test_key };
    const char *untrusted[] = { other_key };
    const char *both[] = { other_key, test_key };
    recovery_manifest_t m;

    printf("A manifest signed by manifest.py\n");
    check_result(load(text, len, trusted, 1, &m), RECOVERY_OK, "verifies against the key that signed it");
    check(strcmp(m.package, "test-1.0") == 0, "reads the package");
    check(m.idcode == 0x0362c093, "reads the IDCODE that the signer took from the bitstream");
    check(strcmp(m.serial, "U64E2-1*") == 0, "reads the serial pattern");
    check(strcmp(m.fpga.name, "fake.bit") == 0 && m.fpga.size == 88, "reads the bitstream name and size");
    check(m.fpga.sha256[0] == 0x53 && m.fpga.sha256[31] == 0x08, "reads the bitstream hash");
    check(strcmp(m.image.name, "fake.ue2") == 0 && m.image.size == 28, "reads the image name and size");
    check(m.image.sha256[0] == 0xe3 && m.image.sha256[31] == 0x62, "reads the image hash");
    check_result(load(text, len, untrusted, 1, &m), RECOVERY_ERR_BAD_SIGNATURE, "is refused by another key");
    check_result(load(text, len, both, 2, &m), RECOVERY_OK, "verifies when its key is second in the list");
    check_result(load(text, len, trusted, 0, &m), RECOVERY_ERR_BAD_SIGNATURE, "is refused with no trusted keys");
    check_result(load(text, len - 1, trusted, 1, &m), RECOVERY_OK, "verifies without its final LF");

    recovery_signature_t sig;
    recovery_manifest_split(text, len, &sig);
    int all_refused = 1;
    for (size_t i = 0; i < sig.signed_len; i++) {
        // Refused, not necessarily as a bad signature: changing the LF before
        // the sig line leaves no signature line to find.
        text[i] ^= 0x01;
        if (load(text, len, trusted, 1, &m) == RECOVERY_OK) {
            printf("        changing byte %zu was not noticed\n", i);
            all_refused = 0;
        }
        text[i] ^= 0x01;
    }
    check(all_refused, "is refused after a change to any one signed byte");
    size_t mid = sig.signed_len + 4 + sig.sig_base64_len / 2;
    text[mid] = (text[mid] == 'A') ? 'B' : 'A';
    check_result(load(text, len, trusted, 1, &m), RECOVERY_ERR_BAD_SIGNATURE, "is refused after a change to the signature");
    free(text);

    text = read_file("any_serial.manifest", &len);
    check_result(load(text, len, trusted, 1, &m), RECOVERY_OK, "without a serial line verifies");
    check(strcmp(m.serial, "*") == 0, "without a serial line applies to every serial");
    free(text);
    free(test_key);
    free(other_key);
}

/* A failing psa_crypto_init() on demand, for the checks that the verifier says
   so rather than calling the manifest badly signed. The Makefile links with
   --wrap=psa_crypto_init, so the calls recovery_verify.c makes land here. */
psa_status_t __real_psa_crypto_init(void);
psa_status_t __real_psa_hash_update(psa_hash_operation_t *operation, const uint8_t *input, size_t input_length);
psa_status_t __real_psa_hash_finish(psa_hash_operation_t *operation, uint8_t *hash, size_t hash_size,
                                    size_t *hash_length);
static psa_status_t crypto_init_forced = PSA_SUCCESS;
static psa_status_t hash_update_forced = PSA_SUCCESS;
static psa_status_t hash_finish_forced = PSA_SUCCESS;
static int crypto_init_calls;

psa_status_t __wrap_psa_hash_update(psa_hash_operation_t *operation, const uint8_t *input, size_t input_length)
{
    if (hash_update_forced != PSA_SUCCESS) {
        return hash_update_forced;
    }
    return __real_psa_hash_update(operation, input, input_length);
}

psa_status_t __wrap_psa_hash_finish(psa_hash_operation_t *operation, uint8_t *hash, size_t hash_size,
                                    size_t *hash_length)
{
    if (hash_finish_forced != PSA_SUCCESS) {
        return hash_finish_forced;
    }
    return __real_psa_hash_finish(operation, hash, hash_size, hash_length);
}

psa_status_t __wrap_psa_crypto_init(void)
{
    crypto_init_calls++;
    if (crypto_init_forced != PSA_SUCCESS) {
        return crypto_init_forced;
    }
    return __real_psa_crypto_init();
}

/* Each way a manifest or an image is refused, by the name it logs. */
static void test_refusals_by_name(void)
{
    size_t len, fpga_len, image_len;
    char *text = read_file("valid.manifest", &len);
    char *test_key = read_file("test_key.pub.pem", NULL);
    char *other_key = read_file("other_key.pub.pem", NULL);
    char *fpga = read_file("fake.bit", &fpga_len);
    char *image = read_file("fake.ue2", &image_len);
    const char *trusted[] = { test_key };
    const char *untrusted[] = { other_key };
    recovery_manifest_t m;
    recovery_signature_t sig;
    recovery_result_t r;

    printf("Refusals, by the error they name\n");
    r = load(text, len, trusted, 1, &m);
    check(r == RECOVERY_OK && strcmp(recovery_result_name(r), "ok") == 0, "a valid manifest is accepted");
    r = load(text, len, untrusted, 1, &m);
    check(r == RECOVERY_ERR_BAD_SIGNATURE && strcmp(recovery_result_name(r), "bad signature") == 0,
          "a wrong key is refused as a bad signature");
    recovery_manifest_split(text, len, &sig);
    char *at = strstr(text, "test-1.0");
    check(at != NULL && at < text + sig.signed_len, "the package line is in the signed text");
    *at ^= 0x01;
    r = load(text, len, trusted, 1, &m);
    check(r == RECOVERY_ERR_BAD_SIGNATURE && strcmp(recovery_result_name(r), "bad signature") == 0,
          "a tampered manifest is refused as a bad signature");
    *at ^= 0x01;

    // The hashes the manifest holds were made from these files.
    recovery_manifest_t good;
    check_result(load(text, len, trusted, 1, &good), RECOVERY_OK, "the manifest verifies again once restored");
    const struct { const recovery_file_t *file; char *data; size_t len; const char *what; } files[] = {
        { &good.image, image, image_len, "image" },
        { &good.fpga, fpga, fpga_len, "bitstream" },
    };
    for (int i = 0; i < 2; i++) {
        char what[96];
        recovery_hash_t h;
        snprintf(what, sizeof(what), "the %s hashes to what its manifest says", files[i].what);
        check(files[i].len == files[i].file->size, "the vector is the size its manifest says");
        check(recovery_hash_start(&h) == RECOVERY_OK, "a hash starts");
        // In three pieces, as the download hands it over.
        size_t a = files[i].len / 3, b = 2 * files[i].len / 3;
        recovery_hash_update(&h, (const uint8_t *)files[i].data, a);
        recovery_hash_update(&h, (const uint8_t *)files[i].data + a, b - a);
        recovery_hash_update(&h, (const uint8_t *)files[i].data + b, files[i].len - b);
        check_result(recovery_hash_finish(&h, files[i].file), RECOVERY_OK, what);

        snprintf(what, sizeof(what), "a changed byte in the %s is refused as a hash mismatch", files[i].what);
        files[i].data[files[i].len / 2] ^= 0x01;
        recovery_hash_start(&h);
        recovery_hash_update(&h, (const uint8_t *)files[i].data, files[i].len);
        r = recovery_hash_finish(&h, files[i].file);
        check(r == RECOVERY_ERR_HASH_MISMATCH && strcmp(recovery_result_name(r), "hash differs") == 0, what);
        files[i].data[files[i].len / 2] ^= 0x01;

        // A compare that stopped early would still see the file change, which
        // changes every byte of the hash; the manifest's hash changing in one
        // byte, at its start, middle or end, is what shows it.
        static const int flips[] = { 0, 16, 31 };
        for (int f = 0; f < 3; f++) {
            recovery_file_t wrong = *files[i].file;
            wrong.sha256[flips[f]] ^= 0x01;
            snprintf(what, sizeof(what), "the %s is refused when byte %d of its manifest hash differs", files[i].what,
                     flips[f]);
            recovery_hash_start(&h);
            recovery_hash_update(&h, (const uint8_t *)files[i].data, files[i].len);
            check_result(recovery_hash_finish(&h, &wrong), RECOVERY_ERR_HASH_MISMATCH, what);
        }

        snprintf(what, sizeof(what), "a %s cut short is refused as a hash mismatch", files[i].what);
        recovery_hash_start(&h);
        recovery_hash_update(&h, (const uint8_t *)files[i].data, files[i].len - 1);
        check_result(recovery_hash_finish(&h, files[i].file), RECOVERY_ERR_HASH_MISMATCH, what);
    }

    // A hash that fails part-way is a crypto error, never "matches": the
    // image whose hash was not computed must not be accepted.
    for (int i = 0; i < 2; i++) {
        recovery_hash_t fh;
        char what[96];
        recovery_hash_start(&fh);
        if (i == 0) {
            hash_update_forced = PSA_ERROR_HARDWARE_FAILURE;
            r = recovery_hash_update(&fh, (const uint8_t *)image, image_len);
            hash_update_forced = PSA_SUCCESS;
            snprintf(what, sizeof(what), "a hash update that fails is reported as a crypto library failure");
            check(r == RECOVERY_ERR_CRYPTO && strcmp(recovery_result_name(r), "crypto library failed") == 0, what);
            recovery_hash_abort(&fh);
        } else {
            recovery_hash_update(&fh, (const uint8_t *)image, image_len);
            hash_finish_forced = PSA_ERROR_HARDWARE_FAILURE;
            r = recovery_hash_finish(&fh, &good.image);
            hash_finish_forced = PSA_SUCCESS;
            snprintf(what, sizeof(what), "a hash finish that fails is a crypto library failure, not a match");
            check(r == RECOVERY_ERR_CRYPTO && strcmp(recovery_result_name(r), "crypto library failed") == 0, what);
        }
    }

    // A trusted key that is not an EC key is refused, even with a signature
    // that is valid for it: rsa512.manifest holds the signed text of
    // valid.manifest with a PKCS#1 v1.5 SHA-256 signature by the RSA-512 key in
    // rsa512.pub.pem (openssl genrsa 512; openssl dgst -sha256 -sign).
    size_t rsa_len;
    char *rsa_text = read_file("rsa512.manifest", &rsa_len);
    char *rsa_key = read_file("rsa512.pub.pem", NULL);
    const char *rsa_trusted[] = { rsa_key };
    r = load(rsa_text, rsa_len, rsa_trusted, 1, &m);
    check(r == RECOVERY_ERR_BAD_SIGNATURE && strcmp(recovery_result_name(r), "bad signature") == 0,
          "a manifest correctly signed by an RSA key is refused as a bad signature");
    free(rsa_text);
    free(rsa_key);

    // The crypto library failing to start is not a bad signature.
    crypto_init_forced = PSA_ERROR_INSUFFICIENT_MEMORY;
    crypto_init_calls = 0;
    r = load(text, len, trusted, 1, &m);
    check(crypto_init_calls > 0, "the verifier initialises the crypto library itself");
    check(r == RECOVERY_ERR_CRYPTO && strcmp(recovery_result_name(r), "crypto library failed") == 0,
          "a crypto library that will not start is named, with a valid manifest");
    check(r != RECOVERY_ERR_BAD_SIGNATURE, "and is not called a bad signature");
    recovery_hash_t h;
    check_result(recovery_hash_start(&h), RECOVERY_ERR_CRYPTO, "a hash cannot start either");
    crypto_init_forced = PSA_SUCCESS;
    check_result(load(text, len, trusted, 1, &m), RECOVERY_OK, "and the manifest verifies once it starts");

    free(text);
    free(test_key);
    free(other_key);
    free(fpga);
    free(image);
}

static void test_split(void)
{
    recovery_signature_t sig;
    char big[RECOVERY_MANIFEST_MAX_SIZE + 2];

    printf("Finding the signature\n");
    check_result(recovery_manifest_split(body, strlen(body), &sig), RECOVERY_ERR_NO_SIGNATURE, "none at all");
    const char *not_last = "format=1\nsig=AAAA\npackage=p\n";
    check_result(recovery_manifest_split(not_last, strlen(not_last), &sig), RECOVERY_ERR_NO_SIGNATURE, "not on the last line");
    check_result(recovery_manifest_split("sig=AAAA\n", 9, &sig), RECOVERY_ERR_NO_SIGNATURE, "with nothing to sign");
    check_result(recovery_manifest_split("format=1\nsig=\n", 14, &sig), RECOVERY_ERR_NO_SIGNATURE, "empty");
    const char *ok = "format=1\nsig=AAAA\n";
    check_result(recovery_manifest_split(ok, strlen(ok), &sig), RECOVERY_OK, "on the last line");
    check(sig.signed_len == 9 && sig.sig_base64_len == 4 && sig.sig_base64 == ok + 13,
          "covers everything up to and including the LF before it");
    memset(big, 'x', sizeof(big));
    check_result(recovery_manifest_split(big, sizeof(big) - 1, &sig), RECOVERY_ERR_TOO_LARGE, "in a manifest over the limit");
}

static void test_parse(void)
{
    recovery_manifest_t m;

    printf("Reading the signed part\n");
    check_result(parse_str(body), RECOVERY_OK, "a complete one");
    recovery_manifest_parse(body, strlen(body), &m);
    check(strcmp(m.serial, "*") == 0, "defaults the serial pattern to '*'");
    check_result(parse_str(""), RECOVERY_ERR_FORMAT, "an empty one");
    check_result(parse_str(with_line("format", "format=2\n")), RECOVERY_ERR_FORMAT, "format 2");
    check_result(parse_str(with_line("format", "")), RECOVERY_ERR_FORMAT, "format not on the first line");
    check_result(parse_str(with_line("package", "package=p\r\n")), RECOVERY_ERR_SYNTAX, "a CR");
    check_result(parse_str(with_line("package", "\n")), RECOVERY_ERR_SYNTAX, "an empty line");
    check_result(parse_str(with_line("package", "package\n")), RECOVERY_ERR_SYNTAX, "a line without '='");
    check_result(parse_str(with_line("package", "=p\n")), RECOVERY_ERR_SYNTAX, "a line without a key");
    check_result(parse_str("format=1\npackage=p"), RECOVERY_ERR_SYNTAX, "a last line without LF");
    check_result(parse_str(with_line("package", "package=p\nnotes=hi\n")), RECOVERY_ERR_UNKNOWN_KEY, "an unknown key");
    check_result(parse_str(with_line("package", "package=p\nsig=AAAA\n")), RECOVERY_ERR_UNKNOWN_KEY, "a sig line in the signed part");
    check_result(parse_str(with_line("package", "package=p\npackage=q\n")), RECOVERY_ERR_DUPLICATE_KEY, "a repeated key");
    check_result(parse_str(with_line("package", "package=p\nformat=1\n")), RECOVERY_ERR_DUPLICATE_KEY, "a repeated format");
    check_result(parse_str(with_line("image.sha256", "")), RECOVERY_ERR_MISSING_KEY, "a missing image hash");
    check_result(parse_str(with_line("fpga.file", "")), RECOVERY_ERR_MISSING_KEY, "a missing bitstream name");

    printf("Values\n");
    check_result(parse_str(with_line("package", "package=\n")), RECOVERY_ERR_BAD_VALUE, "an empty package");
    check_result(parse_str(with_line("package", "package=a b\n")), RECOVERY_ERR_BAD_VALUE, "a package with a space");
    check_result(parse_str(with_line("idcode", "idcode=362c093\n")), RECOVERY_ERR_BAD_VALUE, "a 7 digit IDCODE");
    check_result(parse_str(with_line("idcode", "idcode=0362c09g\n")), RECOVERY_ERR_BAD_VALUE, "an IDCODE that is not hex");
    check_result(parse_str(with_line("idcode", "idcode=0362C093\n")), RECOVERY_OK, "an IDCODE in upper case");
    check_result(parse_str(with_line("fpga.size", "fpga.size=0\n")), RECOVERY_ERR_BAD_VALUE, "a size of 0");
    check_result(parse_str(with_line("fpga.size", "fpga.size=4294967295\n")), RECOVERY_OK, "the largest size");
    check_result(parse_str(with_line("fpga.size", "fpga.size=4294967296\n")), RECOVERY_ERR_BAD_VALUE, "a size over 32 bits");
    check_result(parse_str(with_line("fpga.size", "fpga.size=+1\n")), RECOVERY_ERR_BAD_VALUE, "a signed size");
    check_result(parse_str(with_line("image.sha256", "image.sha256=" HEX64 "0\n")), RECOVERY_ERR_BAD_VALUE, "a hash of 65 digits");
    check_result(parse_str(with_line("fpga.file", "fpga.file=bits/a.bit\n")), RECOVERY_OK, "a file in a subdirectory");
    check_result(parse_str(with_line("fpga.file", "fpga.file=../a.bit\n")), RECOVERY_ERR_BAD_VALUE, "a file in the parent");
    check_result(parse_str(with_line("fpga.file", "fpga.file=x/../../a.bit\n")), RECOVERY_ERR_BAD_VALUE, "a file that climbs out");
    check_result(parse_str(with_line("fpga.file", "fpga.file=/a.bit\n")), RECOVERY_ERR_BAD_VALUE, "an absolute file");
    check_result(parse_str(with_line("fpga.file", "fpga.file=a//b.bit\n")), RECOVERY_ERR_BAD_VALUE, "an empty segment");
    check_result(parse_str(with_line("fpga.file", "fpga.file=a/./b.bit\n")), RECOVERY_ERR_BAD_VALUE, "a '.' segment");
    check_result(parse_str(with_line("fpga.file", "fpga.file=http://x/a.bit\n")), RECOVERY_ERR_BAD_VALUE, "a URL");
    check_result(parse_str(with_line("fpga.file", "fpga.file=..a.bit\n")), RECOVERY_OK, "a name that only starts with '..'");
    check_result(parse_str(with_line("package", "package=p\nserial=\n")), RECOVERY_ERR_BAD_VALUE, "an empty serial pattern");
    check_result(parse_str(with_line("package", "package=p\nserial="
        "0123456789012345678901234567890123456789012345678901234567890123\n")), RECOVERY_ERR_BAD_VALUE, "a 64 character serial pattern");
}

static void test_board(void)
{
    recovery_manifest_t m;
    recovery_manifest_parse(with_line("package", "package=p\nserial=U64E2-1*\n"),
                            strlen(with_line("package", "package=p\nserial=U64E2-1*\n")), &m);

    printf("Which boards a manifest is for\n");
    check(recovery_manifest_for_board(&m, 0x0362c093, "U64E2-1000"), "the named FPGA and a matching serial");
    check(recovery_manifest_for_board(&m, 0x1362c093, "U64E2-1000"), "a newer revision of the same FPGA");
    check(!recovery_manifest_for_board(&m, 0x03631093, "U64E2-1000"), "not another FPGA");
    check(!recovery_manifest_for_board(&m, 0x0362c093, "U64E2-2000"), "not a serial outside the pattern");
    check(!recovery_manifest_for_board(&m, 0x0362c093, "NOSERIAL"), "not the placeholder of a board without serial");
}

static void test_serial_patterns(void)
{
    size_t len;
    char *text = read_file("serial_patterns.txt", &len);
    int cases = 0, agree = 1;

    printf("Serial patterns, against Python's fnmatch\n");
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        if (line[0] == '#') {
            continue;
        }
        char *pattern = strchr(line, '\t') + 1;
        char *serial = strchr(pattern, '\t');
        *serial++ = 0;
        bool expected = (line[0] == '1');
        cases++;
        if (recovery_serial_match(pattern, serial) != expected) {
            printf("        '%s' against '%s': expected %d\n", pattern, serial, expected);
            agree = 0;
        }
    }
    char what[64];
    snprintf(what, sizeof(what), "agree on all %d cases", cases);
    check(cases > 0 && agree, what);
    free(text);
}

/* The keys the board ships with, main and spare: each must load as an EC public
   key, no two may be the same (a spare pasted as a copy of the main key rotates
   nothing), and a manifest must be verified by a key behind all of them. */
static void test_shipped_keys(void)
{
    int n = 0;
    while (recovery_trusted_keys[n]) {
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        check(psa_crypto_init() == PSA_SUCCESS, "the crypto library starts");
        check(mbedtls_pk_parse_public_key(&pk, (const unsigned char *)recovery_trusted_keys[n],
                                          strlen(recovery_trusted_keys[n]) + 1) == 0 &&
              PSA_KEY_TYPE_IS_ECC_PUBLIC_KEY(mbedtls_pk_get_key_type(&pk)),
              "a shipped key loads as an EC public key");
        mbedtls_pk_free(&pk);
        n++;
    }
    check(n >= 2, "a spare key ships next to the main key");
    int distinct = 1;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            distinct &= strcmp(recovery_trusted_keys[i], recovery_trusted_keys[j]) != 0;
        }
    }
    check(distinct, "the shipped keys are all different");

    size_t len;
    char *text = read_file("valid.manifest", &len);
    char *test_key = read_file("test_key.pub.pem", NULL);
    const char *list[16];
    int count = 0;
    for (int i = 0; i < n && i < 15; i++) {
        list[count++] = recovery_trusted_keys[i];
    }
    recovery_manifest_t m;
    check_result(load(text, len, list, count, &m), RECOVERY_ERR_BAD_SIGNATURE,
                 "a manifest signed by a test key is refused by the shipped keys");
    list[count++] = test_key;
    check_result(load(text, len, list, count, &m), RECOVERY_OK,
                 "a manifest is verified by a key behind every shipped key");
    free(text);
    free(test_key);
}

int main(void)
{
    test_signed_vectors();
    test_refusals_by_name();
    test_split();
    test_parse();
    test_board();
    test_serial_patterns();
    test_shipped_keys();  // last: it starts the crypto library, which the tests above leave to the code under test

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
