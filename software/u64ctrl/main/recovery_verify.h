/*
 * recovery_verify.h
 *
 * What the recovery needs from the crypto library: the signature check of
 * recovery_manifest.h, and the SHA-256 of a downloaded file checked against
 * the manifest entry for it. Both are built on PSA Crypto (mbedTLS 4), and both
 * start it themselves, so neither depends on who initialised it first.
 *
 * Unlike recovery_manifest.h this needs the crypto library's headers, so
 * only the files that hash include it.
 */

#ifndef SOFTWARE_U64CTRL_MAIN_RECOVERY_VERIFY_H_
#define SOFTWARE_U64CTRL_MAIN_RECOVERY_VERIFY_H_

#include "psa/crypto.h"
#include "recovery_manifest.h"

typedef struct {
    psa_hash_operation_t op;
} recovery_hash_t;

/* Starts hashing a file. RECOVERY_ERR_CRYPTO when the crypto library will not
   start or will not hash. */
recovery_result_t recovery_hash_start(recovery_hash_t *h);

/* Adds the next `len` bytes of the file. RECOVERY_ERR_CRYPTO on a failure,
   which ends the hash. */
recovery_result_t recovery_hash_update(recovery_hash_t *h, const uint8_t *data, size_t len);

/* Ends the hash and compares it with the manifest's. RECOVERY_OK when they are
   equal, RECOVERY_ERR_HASH_MISMATCH when they differ, RECOVERY_ERR_CRYPTO on a
   failure. Either way the hash is over. */
recovery_result_t recovery_hash_finish(recovery_hash_t *h, const recovery_file_t *expected);

/* Ends a hash that is not going to be finished, e.g. a download that failed.
   Harmless on a hash that is already over. */
void recovery_hash_abort(recovery_hash_t *h);

#endif /* SOFTWARE_U64CTRL_MAIN_RECOVERY_VERIFY_H_ */
