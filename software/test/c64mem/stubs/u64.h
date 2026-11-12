#ifndef U64_H
#define U64_H
#include "c64mem_sim.h"
#define C64_MEMORY_BASE     ((uintptr_t)sim_ram)
#define C64_DMA_MEMONLY     sim_memonly
#define C64_SERVE_CONTROL   sim_serve
#define SERVE_WHILE_STOPPED 0x01
#define U64_BASIC_BASE      ((uintptr_t)sim_basic)
#define U64_KERNAL_BASE     ((uintptr_t)sim_kernal)
#define U64_CHARROM_BASE    ((uintptr_t)sim_charrom)
#endif
