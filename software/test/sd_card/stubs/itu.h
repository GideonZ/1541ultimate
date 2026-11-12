#ifndef SD_STUB_ITU_H
#define SD_STUB_ITU_H
#include "sd_sim.h"
#define ENTER_SAFE_SECTION sd_sim_irq_off();
#define LEAVE_SAFE_SECTION sd_sim_irq_on();
#endif
