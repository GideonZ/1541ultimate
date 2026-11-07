# The red models of the ovf fix (make isr-red): sed expressions that turn a module's handler into a model of it before the fix
ISR_RED_FIXES += ovf ovfr ovfr2 ovfr3 ovfr4
#   ovf    the overflow interrupt is not enabled: it is never seen (the handler before the fix)
#   ovfr   an overflow does not mark the open frame as damaged (no skip)
#   ovfr2  the skip does not end at the first 0xC0
#   ovfr3  the FIFO is read after the loss (not reset, but 'nothing to read' is forgotten)
#   ovfr4  only the FIFO reset is missing
LEGACY_ISR_ovf    := -e 's/ | UART_INTR_RXFIFO_OVF)/)/'
LEGACY_ISR_ovfr   := -e 's/p_uart->rx_resync = 1;/(void)0;/'
LEGACY_ISR_ovfr2  := -e 's/^\( *\)p_uart->rx_resync = 0;/\1(void)0;/'
LEGACY_ISR_ovfr3  := -e 's/uart_hal_rxfifo_rst(/(void)(/' -e 's/uart_intr_status \&= ~(UART_INTR_RXFIFO_OVF | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_FULL);/(void)0;/'
LEGACY_ISR_ovfr4  := -e 's/uart_hal_rxfifo_rst(/(void)(/'
