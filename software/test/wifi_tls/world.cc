/*
 * world.cc: see world.h.
 */
#include <stdarg.h>
#include <string.h>
#include "world.h"

uint16_t sequence_nr;
bool world_ready;
bool world_no_buffer;
bool world_no_transmit;
world_outcome_t world_outcome;
std::string world_trace;
int world_transmits;
int world_calls;
wifi_call_t world_last;
int world_cancel_asked;

static uint32_t rng;
static command_buf_t tx_buf;
static command_buf_t rx_buf;
static uint8_t last_tx[CMD_BUF_SIZE];
static int last_tx_size;
static world_maker_t make_reply;

static void trace(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    world_trace += line;
}

uint32_t world_rand(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return n ? (rng % n) : 0;
}

void world_reset(uint32_t seed, world_maker_t maker)
{
    rng = (seed * 2654435761u) ^ 0x9E3779B9u;
    if (!rng) {
        rng = 1;
    }
    make_reply = maker;
    world_trace.clear();
    world_transmits = 0;
    world_calls = 0;
    world_cancel_asked = 0;
    memset(&world_last, 0, sizeof(world_last));
    last_tx_size = -1;
    sequence_nr = (uint16_t)world_rand(65536);
    world_ready = true;
    world_no_buffer = false;
    world_no_transmit = false;
    world_outcome = WORLD_ANSWERS;
    memset(tx_buf.data, 0xAB, sizeof(tx_buf.data));
    tx_buf.bufnr = 3;
}

bool wifi_module_ready(void)
{
    return world_ready;
}

int wifi_call(const wifi_call_t *call)
{
    world_calls++;
    world_last = *call;
    trace("call %s %u ms%s%s\n", call->name, (unsigned)call->timeout_ms, call->cancel ? " cancel" : "",
          call->use_deadline ? " deadline" : "");
    if (world_no_buffer) {
        trace("getbuffer -> none\n");
        return WIFI_CALL_UNSENT;
    }
    tx_buf.size = 0;
    rpc_header_t *hdr = (rpc_header_t *)tx_buf.data;
    hdr->command = call->command;
    hdr->thread = WORLD_THREAD;
    hdr->sequence = sequence_nr++;
    tx_buf.size = call->fill(tx_buf.data, call->context);
    if (world_no_transmit) {
        trace("transmit failed\n");
        return WIFI_CALL_UNSENT;
    }
    trace("transmit %d:", tx_buf.size);
    for (int i = 0; (i < tx_buf.size) && (i < CMD_BUF_SIZE); i++) {
        trace(" %02x", tx_buf.data[i]);
    }
    trace("\n");
    world_transmits++;
    memcpy(last_tx, tx_buf.data, CMD_BUF_SIZE);
    last_tx_size = tx_buf.size;
    uint16_t sequence = hdr->sequence;

    if (world_outcome == WORLD_RESTARTS) {
        return WIFI_CALL_RESTARTED;
    }
    if (world_outcome == WORLD_SILENT) {
        if (call->cancel) {
            world_cancel_asked++;
            if (call->cancel(call->cancel_context)) {
                return WIFI_CALL_CANCELLED;
            }
        }
        return WIFI_CALL_BUSY;
    }
    make_reply(&rx_buf, last_tx, last_tx_size, 0);
    int result = call->take(rx_buf.data, rx_buf.size, sequence, call->context);
    // As in wifi_cmd.cc: a reply that is not the answer is no answer
    return (result == WIFI_CALL_FOREIGN) ? WIFI_CALL_BUSY : result;
}
