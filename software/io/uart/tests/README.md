# DMA receive recovery regression

Run `python3 software/io/uart/tests/check_rx_rearm.py` on a Linux host with
G++. The runner compiles the exact current `DmaUartInterrupt` body with
AddressSanitizer/UndefinedBehaviorSanitizer. Its substitutes model interrupt
enable/disable registers, RX acknowledgement, DMA buffer assignment and RTOS
buffer queues; this is not an FPGA timing simulation.

The failing scenario starts with simultaneous receive-complete and
buffer-needed interrupts and an empty free pool. The driver processes the
buffer request first and disables it. A successful callback then consumes and
frees the received packet in the ISR. Previously, that callback outcome did
not re-enable requests, so reception could remain stopped despite a free
buffer. The original driver fails the recovered-buffer assertion; rearming
after either callback outcome passes.

Controls cover a rejected packet and a callback retaining the packet until a
later task release. Each buffer must be released exactly once, the DMA must
receive its address again, and interrupts must terminate even while no buffer
is available. CI and the retail build recipe run this regression.
