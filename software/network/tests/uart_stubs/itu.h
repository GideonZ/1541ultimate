#ifndef TEST_UART_ITU_H
#define TEST_UART_ITU_H
#include <stdint.h>
#define CLOCK_FREQ 66666667
#define ITU_IRQ_HIGH_EN 0
#define UART_DATA 0
inline void ioWrite8(unsigned, unsigned) {}
inline uint8_t ioRead8(unsigned) { return 0; }
inline void install_high_irq(int, uint8_t (*)(void*), void*) {}
inline uint16_t getMsTimer() { return 0; }
#endif
