/* tls_rpc.h: what a module firmware's dispatcher needs of the TLS commands (tls_rpc.c).
 *
 * The firmware that builds tls_rpc.c and tls_engine.c provides, on the include path and at link time:
 *   rpc_calls.h              rpc_header_t and rpc_espcmd_resp, and (through it) rpc_tls.h
 *   my_uart.h                my_uart_transmit_packet(), on UART_NUM_1
 *   wifi_modem.h, sntp.h     entropy_source() and entropy_source_t, and the clock the engine reads
 *   cmd_not_implemented()    answers a command the TLS task does not know (the dispatcher's own)
 * and an sdkconfig with the mbedTLS 4 / PSA configuration the engine needs. */

#ifndef TLS_RPC_H_
#define TLS_RPC_H_

#include <stdint.h>
#include "cmd_buffer.h"

#define TLS_TASK_STACK   8192 // twice the stack handshakes were measured to use on the dispatcher
#define TLS_QUEUE_DEPTH  2    // one request outstanding, and a CLOSE behind a step that timed out

void tls_rpc_start(void);                    // create the TLS queue and task, before the dispatcher runs
void tls_rpc_dispatch(command_buf_t *buf);   // CMD_TLS_*: queue for the TLS task, or answer ESP_ERR_TIMEOUT
void tls_rpc_note_block(int64_t ms);         // how long the dispatcher was held by one command
void tls_serve(command_buf_t *buf);          // the TLS task's side: run one command and transmit its reply

#endif /* TLS_RPC_H_ */
