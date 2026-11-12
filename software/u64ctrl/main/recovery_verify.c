/*
 * recovery_verify.c
 *
 * Signature check of a recovery manifest; see recovery_manifest.h. Kept apart
 * from the parser because it is the only part that needs mbedtls.
 */

#include <string.h>
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#if defined(MBEDTLS_USE_PSA_CRYPTO)
#include "psa/crypto.h"
#endif
#include "recovery_manifest.h"

static bool verify_with_key(const char *key_pem, const uint8_t *hash, const uint8_t *sig, size_t sig_len)
{
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    // The PEM parser wants the terminating NUL counted in the length.
    bool ok = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)key_pem, strlen(key_pem) + 1) == 0 &&
              mbedtls_pk_get_type(&pk) == MBEDTLS_PK_ECKEY &&
              mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sig_len) == 0;
    mbedtls_pk_free(&pk);
    return ok;
}

recovery_result_t recovery_manifest_verify(const char *text, const recovery_signature_t *sig,
                                           const char *const *keys_pem, int n_keys)
{
    uint8_t der[RECOVERY_SIG_MAX];
    size_t der_len = 0;
    if (mbedtls_base64_decode(der, sizeof(der), &der_len, (const unsigned char *)sig->sig_base64,
                              sig->sig_base64_len) != 0 || der_len == 0) {
        return RECOVERY_ERR_BAD_SIGNATURE;
    }

#if defined(MBEDTLS_USE_PSA_CRYPTO)
    // pk verifies through PSA in this configuration; repeated calls are harmless.
    if (psa_crypto_init() != PSA_SUCCESS) {
        return RECOVERY_ERR_BAD_SIGNATURE;
    }
#endif

    uint8_t hash[32];
    if (mbedtls_sha256((const unsigned char *)text, sig->signed_len, hash, 0) != 0) {
        return RECOVERY_ERR_BAD_SIGNATURE;
    }
    for (int i = 0; i < n_keys; i++) {
        if (verify_with_key(keys_pem[i], hash, der, der_len)) {
            return RECOVERY_OK;
        }
    }
    return RECOVERY_ERR_BAD_SIGNATURE;
}
