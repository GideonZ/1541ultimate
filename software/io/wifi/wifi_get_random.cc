// wifi_get_random.cc: GET_RANDOM for the UCI command of the same name. Its own file so
// that the programs that link wifi_cmd.cc without wifi.cc (factorytest) have no
// unresolved references.

#include "wifi_cmd.h"
#include "wifi_random.h"

// GET_RANDOM for the UCI command of the same name, called on the network worker.
// BUFARGS would wait up to 1000 ticks (5 s) for a TX buffer; here 4 ticks (20 ms),
// so that the worst case is that plus the 200 ms for the answer. A refusal to
// send or an answer that does not come is "busy": nothing was drawn, asking again is safe.
int wifi_get_random(uint8_t *buf, uint16_t len)
{
    if (!wifi_module_ready()) {
        return WIFI_RANDOM_NO_MODULE;
    }
    command_buf_t *tx;
    if (esp32.uart->GetBuffer(&tx, 4) == pdFALSE) {
        printf(no_wifi_buf);
        return WIFI_RANDOM_BUSY;
    }
    rpc_get_random_req *args = (rpc_get_random_req *)tx->data;
    args->hdr.command = CMD_GET_RANDOM;
    args->length = len;
    tx->size = sizeof(rpc_get_random_req);
    if (!wifi_reply_register(&args->hdr)) {
        esp32.uart->FreeBuffer(tx);
        printf(no_wifi_slot);
        return WIFI_RANDOM_BUSY;
    }
    uint16_t sequence = args->hdr.sequence;
    tx = wifi_transmit(tx, 200);
    if (!tx) {
        printf("*** NO REPLY FROM WIFI MODULE.\n");
        return WIFI_RANDOM_BUSY;
    }
    int retval = wifi_random_take_reply(tx->data, tx->size, sequence, buf, len);
    if (retval != 0) {
        printf("Get Random failed (%d). Size: %d.\n", retval, tx->size);
    }
    esp32.uart->FreeBuffer(tx);
    return retval;
}
