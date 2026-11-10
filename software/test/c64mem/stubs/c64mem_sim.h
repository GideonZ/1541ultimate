// Host stand-ins for what u64/u64_machine.cc uses: the C64 memory window
// is a plain array, C64_DMA_MEMONLY records who sets it, and the critical-section and
// scheduler-suspend calls let the test see how many bytes of C64 memory move inside each.
#ifndef C64MEM_SIM_H
#define C64MEM_SIM_H
#include <stdint.h>
#include <stddef.h>

struct SimMode {
    uint8_t v;
    long writes;             // every write of C64_DMA_MEMONLY
    operator uint8_t() const { return v; }
    SimMode &operator=(uint8_t x) { v = x; writes++; return *this; }
};
extern SimMode sim_memonly;
extern uint8_t sim_serve;
extern uint8_t sim_ram[65536], sim_basic[8192], sim_kernal[8192], sim_charrom[4096];
void sim_enter(void);      // portENTER_CRITICAL
void sim_exit(void);
void sim_suspend(void);    // vTaskSuspendAll
long sim_resume(void);     // xTaskResumeAll
#endif
