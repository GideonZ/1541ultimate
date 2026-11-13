// The scripted world that the host test runs wifi_dedicated.cc in: the module's
// replies, the receive interrupt, the tick, the buffer pool and its resets.
// Every random decision comes from one generator, and every call is traced.
#ifndef WORLD_H_
#define WORLD_H_

#include <string>
#include "wifi_cmd.h"

// Builds a reply of the given kind into rx, as an answer to the last request
// sent (req, req_size; req_size is -1 when nothing has been sent).
typedef void (*world_maker_t)(command_buf_t *rx, const uint8_t *req, int req_size, int kind);

// Starts a new world from seed. With calm set nothing fails at random, and the
// first wait delivers exactly one reply of kind 0, unless a script is given.
void world_reset(uint32_t seed, world_maker_t maker, bool calm);
uint32_t world_rand(uint32_t n);

// A calm world's waits follow a script instead: each step arrives delay ticks
// into the wait, at most MAX_STEP_REPLIES replies, a stale one tagged before a
// pool reset. A step whose delay outlasts the wait is still due in the next one.
#define MAX_STEP_REPLIES 4
typedef struct {
    TickType_t delay;
    int count;
    int kind[MAX_STEP_REPLIES];
    bool stale[MAX_STEP_REPLIES];
    TickType_t preempt; // added to the clock at the next tick read, as if preempted
} world_step_t;
void world_script(const world_step_t *steps, int count);

extern bool world_ready;          // what wifi_module_ready answers
extern bool world_no_buffer;      // GetBuffer fails, even when calm
extern bool world_no_transmit;    // TransmitPacket fails, even when calm
extern std::string world_trace;   // every call, in order
extern int world_suspended;       // vTaskSuspendAll minus xTaskResumeAll
extern int world_transmits;       // requests that went out
extern TickType_t world_transmit_ticks; // how long a calm TransmitPacket takes to queue the request
extern int world_late_after_poll;      // replies the interrupt hands over just after an empty receive 0
extern uint64_t world_wait_span;   // how far past the first tick read after sending any wait reached
size_t world_queued(void);        // replies still in dedicated_replies

#endif /* WORLD_H_ */
