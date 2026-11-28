// wifi_call.h: a request to the WiFi module that another task can end early, and the call that ends it. Free of
// FreeRTOS and of rpc_calls.h (whose names clash with the command interface), so that the network target and the
// host tests can include it. The calls are in wifi_cmd.cc.

#ifndef WIFI_CALL_H_
#define WIFI_CALL_H_

#include <stdint.h>
#include <stdbool.h>

// A request that another task can end early. wifi_call gets a buffer, registers a reply slot, has fill write
// the request after its header, sends it and waits for the reply for timeout_ms. The wait also ends when
// cancel (if any) says so, at the deadline (if any), and when wifi_command_init restarts the channel to the
// module (the other resets of the buffer pool, in esp32.cc, do not wake it: it waits its timeout out, and
// ends BUSY, as no reply can come). A reply that comes
// first is the caller's, whichever of these comes with it: take checks it and returns 0 or its own code,
// and WIFI_CALL_FOREIGN for a reply that is not the answer (the call then ends BUSY).
// One call at a time may wait this way; a second is UNSENT. wifi_wake is for the task that raises a cancel:
// it ends the wait at once, so that nothing has to poll. Only wifi_tls.cc calls it, and that is built for the
// Nios II U64 alone; wifi_cmd.cc compiles it into the RISC-V U64 firmware too, where nothing does.
#define WIFI_CALL_BUSY       1  // sent, but no answer in time; the module may still act on it
#define WIFI_CALL_FOREIGN    5  // from take only: not the answer to this request
#define WIFI_CALL_UNSENT     6  // no buffer, no reply slot, another call waiting, the pool was reset or the UART refused it: nothing was sent
#define WIFI_CALL_CANCELLED  9  // sent, and ended by cancel
#define WIFI_CALL_RESTARTED  10 // sent, and the module restarted: no answer will come
#define WIFI_CALL_SENT       11 // send_only only: sent, nobody waits for the answer

typedef struct {
    const char *name;    // for the log
    uint8_t command;     // CMD_*
    uint32_t timeout_ms; // for the reply, from the moment the request is sent
    // Writes the request's arguments after its header, which is filled in already, and returns the size
    // of the whole request. It may not fail, so the caller checks the arguments first.
    int (*fill)(uint8_t *request, void *context);
    // Checks the size bytes at reply as the answer to request sequence and takes what it needs. Runs with the
    // scheduler suspended, so it may neither block nor print.
    int (*take)(const uint8_t *reply, int size, uint16_t sequence, void *context);
    void *context;
    // Optional: true ends the wait. Asked before every wait, and again after each wake of wifi_wake.
    bool (*cancel)(void *cancel_context);
    void *cancel_context;
    // Optional: the wait ends when the tick counter reaches deadline, if that is before the timeout
    bool use_deadline;
    uint32_t deadline;
    // The request is sent and the call is over: WIFI_CALL_SENT, or WIFI_CALL_UNSENT when nothing went out. Nobody
    // waits for the answer, so timeout_ms, take and cancel are not used. The answer of a TLS command comes to
    // no slot, and wifi_rx_isr drops it; the reply slot is released before this returns.
    bool send_only;
} wifi_call_t;

int wifi_call(const wifi_call_t *call);
// Ends the wait of task, if it waits in wifi_call, and has it ask cancel. Safe from any task.
void wifi_wake(void *task); // a TaskHandle_t, which is not a void * in FreeRTOS: this header has none

#endif /* WIFI_CALL_H_ */
