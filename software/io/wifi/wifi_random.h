// wifi_random.h: hardware random bytes from the WiFi module. Apart from
// wifi_cmd.h, whose rpc_calls.h clashes with the command interface (CMD_IDENTIFY).

#ifndef WIFI_RANDOM_H_
#define WIFI_RANDOM_H_

#include <stdint.h>

// Fills buf with len bytes from the module's hardware random number generator
// and returns 0, or returns one of the codes below and leaves buf untouched.
// U64 only.
int wifi_get_random(uint8_t *buf, uint16_t len);
#define WIFI_RANDOM_BUSY      1 // no answer in time; the module may be busy
#define WIFI_RANDOM_REFUSED   2 // the module refused, or had no entropy source
#define WIFI_RANDOM_INVALID   3 // the answer is not a well formed reply
#define WIFI_RANDOM_NO_MODULE 4 // the module application is not running

// Checks size bytes at reply as the answer to request sequence for len bytes.
// Only a well formed answer of exactly len bytes from a real source returns 0
// and copies the bytes to buf; nothing else touches buf.
int wifi_random_take_reply(const uint8_t *reply, int size, uint16_t sequence,
                           uint8_t *buf, uint16_t len);
#define WIFI_RANDOM_FOREIGN   5 // not the answer to this request

// The UCI status for a result of wifi_get_random: 0 (the bytes go out) for 0 only,
// 88 (busy) for WIFI_RANDOM_BUSY, 86 (internal error) for WIFI_RANDOM_INVALID, 87 otherwise.
int wifi_random_status(int result);

#endif /* WIFI_RANDOM_H_ */
