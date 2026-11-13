# The red models of the tx fix (make isr-red): sed expressions that turn a module's handler into a model of it before the fix
ISR_RED_FIXES += txdirect txone txnoena txpartial
#   txdirect   my_uart_transmit_packet does not push the frame itself (it only enables the interrupt)
#   txone      the fill stops after one frame
#   txnoena    the handler does not ask for the interrupt again for what did not fit
#   txpartial  a full FIFO is reported as nothing left
LEGACY_ISR_txdirect  := -e 's/if (my_uart_tx_fill(obj, uart_num, \&woken)) {/if (1) {/'
LEGACY_ISR_txone     := -e 's/p_uart->current_tx_buf = NULL; \/\/ currently not transmitting/p_uart->current_tx_buf = NULL; return true;/'
LEGACY_ISR_txnoena   := -e 's/if (my_uart_tx_fill(p_uart, uart_num, \&HPTaskAwoken)) {/if (my_uart_tx_fill(p_uart, uart_num, \&HPTaskAwoken) \&\& 0) {/'
LEGACY_ISR_txpartial := -e 's/return true; \/\/ the FIFO is full/return false;/'
