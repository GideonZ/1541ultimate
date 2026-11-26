#pragma once
#include <stdint.h>
extern uint8_t command_registers[4096];
#define CMD_IF_BASE ((uintptr_t)command_registers)
#define ITU_IRQ_DISABLE 0
#define ITU_IRQ_CLEAR 1
#define ITU_IRQ_ENABLE 2
#define ITU_INTERRUPT_CMDIF 4
#define ITU_INTERRUPT_RESET 8
inline void ioWrite8(int, int) {}
