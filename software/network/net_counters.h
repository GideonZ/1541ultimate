/*
 * net_counters.h
 *
 * Counters for the host network path, to be read back from the console / syslog ("NETCTR" lines,
 * network_interface.cc). Plain 32-bit counters, no locks: two contexts counting at once may lose a
 * count, which is fine for a diagnostic. Every program that builds one of the counting files links
 * without a makefile change, because the variable is defined (weakly) in this header.
 *
 * The header lives in network/ because that directory is on the include path of the lwIP libraries
 * as well as of the applications.
 *
 * The counters are always compiled in (a few increments; other code reads them). The reporter task that
 * prints them is not: it exists only when NET_COUNTERS_REPORT is 1, which only a bench build sets. A
 * printed line is a console/syslog line, and over WiFi a syslog line is a frame that changes the counters
 * the reporter prints.
 */

#ifndef NET_COUNTERS_H_
#define NET_COUNTERS_H_

#include <stdint.h>

#ifndef NET_COUNTERS_REPORT
#define NET_COUNTERS_REPORT 0   // 1: network_interface.cc runs the task that prints NETCTR (bench builds)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WTX_BUCKETS   6
#define NET_WTX_TIMEOUT   5  // index of the "no buffer in time" bucket

typedef struct {
    // WiFi linkoutput waits for a UART TX buffer, in ticks: 0, 1, 2-9, 10-99, 100-999, timed out
    uint32_t wifi_tx_wait[NET_WTX_BUCKETS];
    uint32_t wifi_tx_wait_max;      // ticks, the longest wait (a timeout counts too)
    uint32_t uart_rx_overflow;      // DMA UART status bit 3 seen
    uint32_t uart_rx_long;          // received frames longer than CMD_BUF_SIZE
} net_counters_t;

__attribute__((weak)) net_counters_t net_counters;

// The bucket of a WiFi TX buffer wait that did get a buffer
static inline int net_counters_wtx_bucket(uint32_t ticks)
{
    if (ticks < 2) {
        return (int)ticks;
    }
    return (ticks < 10) ? 2 : (ticks < 100) ? 3 : 4;
}

// Count one wait of 'ticks'; 'got_buffer' is false when it timed out
static inline void net_counters_wifi_tx_wait(uint32_t ticks, int got_buffer)
{
    net_counters.wifi_tx_wait[got_buffer ? net_counters_wtx_bucket(ticks) : NET_WTX_TIMEOUT]++;
    if (ticks > net_counters.wifi_tx_wait_max) {
        net_counters.wifi_tx_wait_max = ticks;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* NET_COUNTERS_H_ */
