#ifndef ULTIMATE_TLS_EPOCH_H
#define ULTIMATE_TLS_EPOCH_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS_EPOCH_RECORD_SIZE 16
/* Storage is private device state, never configuration to export/restore.
 * load returns 1 for an exact record, 0 for absent, -1 for an I/O/size error.
 * save must synchronously persist a complete record. Slots must be separate;
 * the highest committed slot is never overwritten when advancing the epoch.
 * No epoch may be used until advance has succeeded, including readback.
 */
typedef struct {
    void *context;
    int (*load)(void *, unsigned slot, uint8_t *record);
    bool (*save)(void *, unsigned slot, const uint8_t *record);
} tls_epoch_store;

bool tls_epoch_advance(const tls_epoch_store *, uint32_t *boot);
bool tls_epoch_next(uint32_t boot, uint32_t *counter, uint64_t *epoch);

#ifdef __cplusplus
}
#endif
#endif
