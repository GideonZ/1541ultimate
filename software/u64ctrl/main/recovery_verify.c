/*
 * recovery_verify.c
 *
 * Signature check of a recovery manifest; see recovery_manifest.h. Kept apart
 * from the parser because it is the only part that needs mbedtls.
 */

#include <string.h>
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "psa/crypto.h"
#include "recovery_manifest.h"
#include "recovery_verify.h"

static bool verify_with_key(const char *key_pem, const uint8_t *hash, const uint8_t *sig, size_t sig_len)
{
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    // The PEM parser wants the terminating NUL counted in the length.
    bool ok = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)key_pem, strlen(key_pem) + 1) == 0 &&
              PSA_KEY_TYPE_IS_ECC_PUBLIC_KEY(mbedtls_pk_get_key_type(&pk)) &&
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

    // pk and the hash both go through PSA; repeated calls are harmless.
    if (psa_crypto_init() != PSA_SUCCESS) {
        return RECOVERY_ERR_CRYPTO;
    }

    uint8_t hash[32];
    size_t hash_len = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)text, sig->signed_len, hash, sizeof(hash),
                         &hash_len) != PSA_SUCCESS || hash_len != sizeof(hash)) {
        return RECOVERY_ERR_CRYPTO;
    }
    for (int i = 0; i < n_keys; i++) {
        if (verify_with_key(keys_pem[i], hash, der, der_len)) {
            return RECOVERY_OK;
        }
    }
    return RECOVERY_ERR_BAD_SIGNATURE;
}

recovery_result_t recovery_hash_start(recovery_hash_t *h)
{
    h->op = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
    if (psa_crypto_init() != PSA_SUCCESS || psa_hash_setup(&h->op, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        return RECOVERY_ERR_CRYPTO;
    }
    return RECOVERY_OK;
}

recovery_result_t recovery_hash_update(recovery_hash_t *h, const uint8_t *data, size_t len)
{
    if (psa_hash_update(&h->op, data, len) != PSA_SUCCESS) {
        psa_hash_abort(&h->op);
        return RECOVERY_ERR_CRYPTO;
    }
    return RECOVERY_OK;
}

recovery_result_t recovery_hash_finish(recovery_hash_t *h, const recovery_file_t *expected)
{
    uint8_t hash[32];
    size_t hash_len = 0;
    if (psa_hash_finish(&h->op, hash, sizeof(hash), &hash_len) != PSA_SUCCESS || hash_len != sizeof(hash)) {
        psa_hash_abort(&h->op);
        return RECOVERY_ERR_CRYPTO;
    }
    return memcmp(hash, expected->sha256, sizeof(hash)) == 0 ? RECOVERY_OK : RECOVERY_ERR_HASH_MISMATCH;
}

void recovery_hash_abort(recovery_hash_t *h)
{
    psa_hash_abort(&h->op);
}
