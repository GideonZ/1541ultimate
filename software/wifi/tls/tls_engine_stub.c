/* tls_engine_stub.c: stands in for tls_engine.c in a size-measurement build (idf.py -D BRIDGE_TLS_STUB=1). The RPC
 * commands stay and every TLS call answers ESP_ERR_NOT_SUPPORTED, so mbedTLS's TLS and X.509 code is not linked.
 * Never flashed to a device. */

#include <string.h>
#include "esp_err.h"
#include "tls_engine.h"

int tls_open(uint8_t slot, uint16_t tag, const tls_open_params_t *params, uint8_t *reason)
{
    (void)slot;
    (void)tag;
    (void)params;
    *reason = TLS_OPEN_REASON_NONE;
    return ESP_ERR_NOT_SUPPORTED;
}

int tls_step(uint8_t slot, uint16_t tag, uint8_t flags, const uint8_t *ct_in, int ct_in_len, const uint8_t *pt_in,
             int pt_in_len, uint8_t *out, uint16_t *ct_out_len, uint16_t *pt_out_len, tls_step_result_t *result)
{
    (void)slot;
    (void)tag;
    (void)flags;
    (void)ct_in;
    (void)ct_in_len;
    (void)pt_in;
    (void)pt_in_len;
    (void)out;
    *ct_out_len = 0;
    *pt_out_len = 0;
    memset(result, 0, sizeof(tls_step_result_t));
    result->state = TLS_STATE_CLOSED;
    result->alert_in = TLS_ALERT_NONE;
    result->alert_out = TLS_ALERT_NONE;
    return ESP_ERR_NOT_FOUND;
}

int tls_close(uint8_t slot, uint16_t tag, uint8_t *out, uint16_t *ct_out_len)
{
    (void)slot;
    (void)tag;
    (void)out;
    *ct_out_len = 0;
    return ESP_ERR_NOT_FOUND;
}

void tls_close_all(void)
{
}

int tls_set_ca(const uint8_t *der, int der_len, uint16_t ca_tag, uint8_t *reject)
{
    (void)der;
    (void)der_len;
    (void)ca_tag;
    *reject = TLS_CA_REJECT_NONE;
    return ESP_ERR_NOT_SUPPORTED;
}

int tls_get_stats(uint8_t slot, uint16_t tag, tls_session_stats_t *stats)
{
    (void)slot;
    (void)tag;
    memset(stats, 0, sizeof(tls_session_stats_t));
    stats->state = TLS_STATE_CLOSED;
    return ESP_ERR_NOT_SUPPORTED;
}

int tls_sessions_open(void)
{
    return 0;
}
