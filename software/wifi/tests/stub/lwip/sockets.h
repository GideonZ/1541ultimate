// Stand-in for lwIP's sockets.h on the build host: rpc_calls.h needs only u16_t.
#ifndef WIFI_TESTS_LWIP_SOCKETS_H
#define WIFI_TESTS_LWIP_SOCKETS_H
#include <stdint.h>
typedef uint16_t u16_t;
#endif
