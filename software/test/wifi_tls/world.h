// The scripted world the host tests run wifi_dedicated.cc in: module replies, receive interrupt, tick, buffer pool and
// resets. Every decision comes from one generator, so two implementations that make the same calls in the same
// order see the same world; they are compared on the trace of those calls.
#ifndef WORLD_H_
#define WORLD_H_

#include <string>
#include "wifi_cmd.h"

// Builds a reply of the given kind into rx, as an answer to the last request
// sent (req, req_size; req_size is -1 when nothing has been sent). kind 0 is
// the right answer; the others are up to the maker.
typedef void (*world_maker_t)(command_buf_t *rx, const uint8_t *req, int req_size, int kind);

// Starts a new world from seed. With calm set nothing fails and the first
// wait delivers exactly one right answer.
void world_reset(uint32_t seed, world_maker_t maker, bool calm);
uint32_t world_rand(uint32_t n);

extern bool world_ready;          // what wifi_module_ready answers
extern bool world_no_buffer;      // GetBuffer fails, even when calm
extern bool world_no_transmit;    // TransmitPacket fails, even when calm
extern std::string world_trace;   // every call, in order
extern int world_suspended;       // vTaskSuspendAll minus xTaskResumeAll
extern int world_transmits;       // requests that went out
extern int world_isr_calls;       // replies the interrupt handled
size_t world_queued(void);        // replies still in dedicated_replies
void world_deliver_now(void);     // the receive interrupt hands over an answer now
void world_reset_maker(world_maker_t maker); // answers from now on come from maker

#endif /* WORLD_H_ */
