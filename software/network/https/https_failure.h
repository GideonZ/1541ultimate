#ifndef HTTPS_FAILURE_H
#define HTTPS_FAILURE_H
#include <stdint.h>

/* Caller serializes updates/snapshots. Epochs strictly increase; late work from
 * an older request and wrapper/cleanup failures cannot replace primary evidence. */
typedef struct {
    uint64_t epoch;
    uint32_t session, count;
    const char *stage;
    int code, detail;
} https_failure;

static inline void https_failure_record(https_failure *out, uint64_t epoch,
                                       uint32_t session, const char *stage, int code, int detail)
{
    if (epoch<=out->epoch) return;
    out->epoch=epoch; out->session=session;
    if (out->count<INT32_MAX) ++out->count;
    out->stage=stage; out->code=code; out->detail=detail;
}
#endif
