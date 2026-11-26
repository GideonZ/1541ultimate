// The scripted world the host tests run wifi_tls.cc in: the module, and wifi_call, which sends a request to it and
// hands the answer to the caller's take. The real wifi_call (wifi_cmd.cc) is tested in test/wifi_reply; here it
// is a stand-in with the same contract, so that what wifi_tls.cc puts into each request and makes of each result
// can be checked on its own. Every decision comes from one generator.
#ifndef WORLD_H_
#define WORLD_H_

#include <string>
#include "wifi_call.h"
#include "rpc_calls.h"
extern "C" {
    #include "cmd_buffer.h"
}

// The reply slot the stand-in puts in the header of each request.
#define WORLD_THREAD 3

// Builds a reply of the given kind into rx, as an answer to the last request
// sent (req, req_size; req_size is -1 when nothing has been sent). kind 0 is
// the right answer; the others are up to the maker.
typedef void (*world_maker_t)(command_buf_t *rx, const uint8_t *req, int req_size, int kind);

// What the module does with the request.
enum world_outcome_t {
    WORLD_ANSWERS,    // the maker's answer is handed to take
    WORLD_SILENT,     // no answer: BUSY once cancel (if any) has been asked
    WORLD_RESTARTS    // the module restarts during the wait
};

void world_reset(uint32_t seed, world_maker_t maker);
uint32_t world_rand(uint32_t n);

extern uint16_t sequence_nr;      // the sequence of the next request
extern bool world_ready;          // what wifi_module_ready answers
extern bool world_no_buffer;      // there is no buffer: UNSENT
extern bool world_no_transmit;    // the UART refuses the request: UNSENT
extern world_outcome_t world_outcome;
extern std::string world_trace;   // every call, in order
extern int world_transmits;       // requests that went out
extern int world_calls;           // calls of wifi_call
extern wifi_call_t world_last;    // the last call's arguments
extern int world_cancel_asked;    // times the stand-in asked the call's cancel function

#endif /* WORLD_H_ */
