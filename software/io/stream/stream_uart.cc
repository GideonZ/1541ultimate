
#include "itu.h"
#include "stream_uart.h"

int Stream_UART :: read(char *buffer, int length)
{
    return (int)uart_read_buffer(buffer, (uint16_t)length);
}

int Stream_UART :: write(const char *buffer, int length)
{
    return (int)uart_write_buffer(buffer, (uint16_t)length);
}

int Stream_UART :: get_char(void)
{
	if (ioRead8(UART_FLAGS) & UART_RxDataAv)
        return uart_get_byte(0);
    else
        return -1;
}

void Stream_UART :: charout(int c)
{
    if(c == '\n') {
    	while (ioRead8(UART_FLAGS) & UART_TxFifoFull);
    	ioWrite8(UART_DATA, '\r');
    }
	while (ioRead8(UART_FLAGS) & UART_TxFifoFull);
	ioWrite8(UART_DATA, c);
}
