#ifndef TLS_METRICS_H
#define TLS_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Experimental, read-only sideband. No pointers, padding or host endianness
 * cross the UART. Independent of TLS session/reservation/reply queues. */
enum {
    TLS_METRIC_INTERNAL_FREE, TLS_METRIC_INTERNAL_MIN, TLS_METRIC_INTERNAL_LARGEST,
    TLS_METRIC_8BIT_FREE, TLS_METRIC_8BIT_MIN, TLS_METRIC_8BIT_LARGEST,
    TLS_METRIC_STACK_MIN_BYTES, TLS_METRIC_UPTIME_SECONDS,
    TLS_METRIC_BOOT_ID, TLS_METRIC_SEQUENCE,
    TLS_METRIC_FAILURE_COUNT, TLS_METRIC_FAILURE_EPOCH_LO, TLS_METRIC_FAILURE_EPOCH_HI,
    TLS_METRIC_FAILURE_SESSION, TLS_METRIC_FAILURE_STAGE, TLS_METRIC_FAILURE_STATUS,
    TLS_METRIC_FAILURE_CODE, TLS_METRIC_FAILURE_VERIFY_FLAGS, TLS_METRIC_COUNT
};
#define TLS_METRICS_V1_COUNT 10u
#define TLS_METRICS_SIZE (4u + 4u * TLS_METRIC_COUNT)
#define TLS_METRICS_MAX_AGE_MS 30000u
typedef struct { uint32_t value[TLS_METRIC_COUNT]; uint32_t version; } tls_metrics;

static inline void tls_metrics_encode(uint8_t *out, const tls_metrics *sample)
{
    out[0]='T'; out[1]='M'; out[2]=2; out[3]=0;
    for (unsigned i=0; i<TLS_METRIC_COUNT; ++i)
        for (unsigned byte=0; byte<4; ++byte)
            out[4+4*i+byte]=(uint8_t)(sample->value[i]>>(8*byte));
}

static inline bool tls_metrics_decode(tls_metrics *out, const uint8_t *in, size_t size)
{
    if (!out || !in || size<4 || in[0]!='T' || in[1]!='M' || in[3]!=0) return false;
    unsigned count=in[2]==1 ? TLS_METRICS_V1_COUNT : TLS_METRIC_COUNT;
    if ((in[2]!=1 && in[2]!=2) || size!=4u+4u*count) return false;
    out->version=in[2];
    for (unsigned i=0; i<TLS_METRIC_COUNT; ++i) {
        out->value[i]=0;
        if (i<count)
            for (unsigned byte=0; byte<4; ++byte)
                out->value[i]|=(uint32_t)in[4+4*i+byte]<<(8*byte);
    }
    return true;
}
#endif
