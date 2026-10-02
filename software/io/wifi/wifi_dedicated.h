// wifi_dedicated.h: requests whose replies bypass tasksWaitingForReply and are
// awaited with a deadline, one at a time. Free of rpc_calls.h and FreeRTOS.

#ifndef WIFI_DEDICATED_H_
#define WIFI_DEDICATED_H_

#include <stdint.h>

// What wifi_dedicated_call returns itself. Anything else comes from take.
// Callers keep their own numbers for these, and assert that they match.
#define DEDICATED_BUSY      1 // no answer in time, or not sent (channel taken, no buffer); retry
#define DEDICATED_NO_MODULE 4 // the module application is not running; nothing was sent
#define DEDICATED_FOREIGN   5 // from take only: not the answer to this request

typedef struct {
    const char *name;    // for the log
    uint8_t command;     // CMD_*
    uint32_t timeout_ms; // for the answer, from the moment the request is queued
    // Writes the arguments after the header and returns the request's size.
    // It may not fail, so the caller checks the arguments first.
    int (*fill)(uint8_t *request, void *context);
    // Takes what it needs from the answer to request sequence, or returns
    // DEDICATED_FOREIGN. Runs with the scheduler suspended: no blocking, no printing.
    int (*take)(const uint8_t *reply, int size, uint16_t sequence, void *context);
    void *context;
} dedicated_call_t;

// Sends one request and waits for its answer. U64 only.
int wifi_dedicated_call(const dedicated_call_t *call);

// Whether the module application has been detected. In wifi.cc.
bool wifi_module_ready(void);

#endif /* WIFI_DEDICATED_H_ */
