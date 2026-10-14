// wifi_dedicated.cc: the one waiter for replies on WIFI_DEDICATED_THREAD, and GET_RANDOM, its
// first user. Not in wifi_cmd.cc, which the updater compiles without wifi.cc.

#include <stddef.h>
#include "wifi_cmd.h"
#include "wifi_dedicated.h"
#include "wifi_tls.h"
#include "wifi_random.h"

static_assert(WIFI_RANDOM_BUSY == DEDICATED_BUSY, "codes differ");
static_assert(WIFI_RANDOM_NO_MODULE == DEDICATED_NO_MODULE, "codes differ");
static_assert(WIFI_RANDOM_FOREIGN == DEDICATED_FOREIGN, "codes differ");
// The log reads esp_err where every reply that has one keeps it.
static_assert(sizeof(rpc_espcmd_resp) == offsetof(rpc_get_random_resp, source), "rpc_espcmd_resp changed");

// Checks a queued reply against the request, lets take have it if it is the answer, and frees it. A reply queued
// before the last buffer pool reset is dropped unread: its buffer may since belong to someone else. The scheduler
// stays suspended from the check until the free, so no reset (only tasks do one) gets in between. check false: free.
static int take_dedicated_reply(const dedicated_call_t *call, dedicated_reply_t *reply, bool check, uint16_t sequence)
{
    int retval = DEDICATED_FOREIGN;
    int size = 0;
    int esp_err = 0;
    vTaskSuspendAll();
    if (reply->generation == esp32.uart->generation) {
        command_buf_t *packet = reply->buf;
        if (check) {
            size = packet->size;
            if (size >= (int)sizeof(rpc_espcmd_resp)) {
                esp_err = ((rpc_espcmd_resp *)packet->data)->esp_err;
            }
            retval = call->take(packet->data, size, sequence, call->context);
        }
        esp32.uart->FreeBuffer(packet); // does not block
    }
    xTaskResumeAll();
    if ((retval != 0) && (retval != DEDICATED_FOREIGN)) {
        if (size >= (int)sizeof(rpc_espcmd_resp)) {
            printf("%s failed (%d). Size: %d. Error: %d.\n", call->name, retval, size, esp_err);
        } else {
            printf("%s failed (%d). Size: %d.\n", call->name, retval, size);
        }
    }
    return retval;
}

// Unlike the calls in wifi_cmd.cc this neither waits for the module forever nor trusts what comes back: take decides
// what the answer is. Between calls dedicated_waiting is clear and dedicated_replies is empty, so no reply is left
// over for the next one.
int wifi_dedicated_call(const dedicated_call_t *call)
{
    // Without the module application nothing can answer, so fail at once
    // rather than on a timeout.
    if (!wifi_module_ready() || !dedicated_replies) {
        return DEDICATED_NO_MODULE;
    }
    // One request at a time: a second caller fails rather than waits.
    bool busy;
    ENTER_SAFE_SECTION;
    busy = dedicated_waiting;
    dedicated_waiting = true;
    LEAVE_SAFE_SECTION;
    if (busy) {
        return DEDICATED_UNSENT;
    }

    int retval = DEDICATED_BUSY;
    bool sent = false;
    bool cancelled = false;
    uint16_t sequence = sequence_nr++;
    dedicated_reply_t reply;
    command_buf_t *packet;
    if (esp32.uart->GetBuffer(&packet, 4) == pdFALSE) { // 20 ms
        printf(no_wifi_buf);
    } else {
        rpc_header_t *hdr = (rpc_header_t *)packet->data;
        hdr->command = call->command;
        hdr->sequence = sequence;
        hdr->thread = WIFI_DEDICATED_THREAD;
        packet->size = call->fill(packet->data, call->context);

        if (esp32.uart->TransmitPacket(packet) == pdTRUE) {
            // A late reply to an earlier request is skipped, within the same
            // timeout.
            sent = true;
            const TickType_t timeout = pdMS_TO_TICKS(call->timeout_ms);
            const TickType_t slice = pdMS_TO_TICKS(DEDICATED_SLICE_MS);
            TickType_t start = xTaskGetTickCount();
            retval = DEDICATED_FOREIGN;
            while (retval == DEDICATED_FOREIGN) {
                TickType_t spent = xTaskGetTickCount() - start;
                if (spent >= timeout) {
                    retval = DEDICATED_BUSY;
                    break;
                }
                TickType_t wait = timeout - spent;
                if (call->cancel && (wait > slice)) {
                    wait = slice;
                }
                if (xQueueReceive(dedicated_replies, &reply, wait) == pdFALSE) {
                    if (call->cancel && call->cancel(call->cancel_context)) {
                        cancelled = true;
                        retval = DEDICATED_BUSY;
                        break;
                    }
                    if (!call->cancel) {
                        retval = DEDICATED_BUSY;
                        break;
                    }
                    continue;
                }
                retval = take_dedicated_reply(call, &reply, true, sequence);
            }
        }
    }

    // Stop the interrupt from queueing replies, then release what it queued.
    // After a timeout that can still be our reply.
    dedicated_waiting = false;
    while (xQueueReceive(dedicated_replies, &reply, 0) == pdTRUE) {
        if ((retval == DEDICATED_BUSY) && !cancelled) {
            retval = take_dedicated_reply(call, &reply, true, sequence);
            if (retval == DEDICATED_FOREIGN) {
                retval = DEDICATED_BUSY;
            }
        } else {
            take_dedicated_reply(call, &reply, false, 0);
        }
    }
    if (cancelled) {
        printf("%s: cancelled.\n", call->name);
    } else if (retval == DEDICATED_BUSY) {
        printf("%s: no reply.\n", call->name);
        if (!sent) {
            retval = DEDICATED_UNSENT;
        }
    }
    return retval;
}

uint32_t wifi_tls_generation(void)
{
    return esp32.uart->generation;
}

typedef struct {
    uint8_t *buf;
    uint16_t len;
} random_call_t;

static int fill_random(uint8_t *request, void *context)
{
    rpc_get_random_req *args = (rpc_get_random_req *)request;
    args->length = ((random_call_t *)context)->len;
    return sizeof(rpc_get_random_req);
}

static int take_random(const uint8_t *reply, int size, uint16_t sequence, void *context)
{
    random_call_t *c = (random_call_t *)context;
    return wifi_random_take_reply(reply, size, sequence, c->buf, c->len);
}

// A C64 program is waiting on the UCI for this one. Bytes are only handed out
// when the reply is the answer to this request and carries all of them.
int wifi_get_random(uint8_t *buf, uint16_t len)
{
    random_call_t args = { buf, len };
    // About 1 ms on the wire; far longer only while the module's dispatcher is
    // blocked handing a scan or connect to its connector, which is worth a retry.
    const dedicated_call_t call = { "Get Random", CMD_GET_RANDOM, 200, fill_random, take_random, &args };
    int r = wifi_dedicated_call(&call);
    // The channel was taken or there was no buffer: nothing was sent, which a caller of GET_RANDOM reads as busy
    return (r == DEDICATED_UNSENT) ? WIFI_RANDOM_BUSY : r;
}
