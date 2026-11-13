// wifi_dedicated.h: requests to the WiFi module that do not trust it to answer, or to answer only them (the TLS
// calls). Replies come through their own queue, not the reply slots; the caller waits with a deadline,
// and one request is outstanding at a time. Free of rpc_calls.h and FreeRTOS, so it can be tested on the host.

#ifndef WIFI_DEDICATED_H_
#define WIFI_DEDICATED_H_

#include <stdint.h>

// What wifi_dedicated_call returns itself. Anything else comes from take.
// Callers keep their own numbers for these, and assert that they match.
#define DEDICATED_BUSY      1 // sent, but no answer in time; the module may still act on it
#define DEDICATED_NO_MODULE 4 // the module application is not running; nothing was sent
#define DEDICATED_FOREIGN   5 // from take only: not the answer to this request
#define DEDICATED_UNSENT    6 // another request was outstanding, or no buffer; nothing was sent

typedef struct {
    const char *name;    // for the log
    uint8_t command;     // CMD_*
    uint32_t timeout_ms; // for the answer, from the moment the request is queued
    // Writes the request's arguments after its header, which is filled in
    // already, and returns the size of the whole request. It may not fail,
    // so the caller checks the arguments first.
    int (*fill)(uint8_t *request, void *context);
    // Checks the size bytes at reply as the answer to request sequence and takes what it needs; returns
    // DEDICATED_FOREIGN for a reply that is not the answer, which is skipped. Runs with the scheduler suspended,
    // so it may neither block nor print.
    int (*take)(const uint8_t *reply, int size, uint16_t sequence, void *context);
    void *context;
    // Optional: the wait is cut into slices of at most DEDICATED_SLICE_MS and cancel is asked after each one that
    // brought no answer; true ends the call with DEDICATED_BUSY, and a later answer is dropped by its sequence
    // number. NULL waits the whole timeout in one go.
    bool (*cancel)(void *cancel_context);
    void *cancel_context;
} dedicated_call_t;

#define DEDICATED_SLICE_MS 100

// Sends one request and waits for its answer. U64 == 1 and U64 == 2 (the Ultimate 64 Elite II and the C64 Ultimate) only.
int wifi_dedicated_call(const dedicated_call_t *call);

// Whether the module application has been detected. In wifi.cc.
bool wifi_module_ready(void);

#endif /* WIFI_DEDICATED_H_ */
