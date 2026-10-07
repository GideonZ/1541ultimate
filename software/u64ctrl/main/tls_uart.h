#ifndef U64CTRL_TLS_UART_H
#define U64CTRL_TLS_UART_H
#include "cmd_buffer.h"
void tls_uart_start(void);
/* Consumes and releases the received buffer on all paths. */
void tls_uart_receive(command_buf_t *);
#endif
