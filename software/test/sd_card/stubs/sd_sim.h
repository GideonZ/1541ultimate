// Host stand-ins for what io/sd_card/sd_card.cc and sdio.cc use: the SDIO registers are
// backed by a simulated SD card in SPI mode, and the RTOS calls are recorded, so a test can
// see what runs with interrupts off, what runs under the card's mutex, and where it yields.
#ifndef SD_SIM_H
#define SD_SIM_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct SdSimData   { operator uint8_t(); SdSimData &operator=(uint8_t v); };
struct SdSimData32 { operator uint32_t(); SdSimData32 &operator=(uint32_t v); };
extern SdSimData sd_sim_data;
extern SdSimData32 sd_sim_data32;
extern volatile uint8_t sd_sim_speed, sd_sim_ctrl, sd_sim_crc;

// what the test inspects
struct SdSimStats {
    long accesses;            // SDIO data register accesses
    long accesses_irq_off;    // ... made with interrupts off
    long longest_irq_off;     // most accesses in one interrupts-off stretch
    long accesses_unlocked;   // ... made in read/write without holding the card's mutex
    long yields, yields_irq_off, yields_unlocked;
    int  irq_depth, lock_depth;
    long stretch;
    // the mutex is a real non-recursive one, as in FreeRTOS: these are misuse counters
    long takes, retakes;      // retakes: taken again by the task that holds it (a deadlock on the target)
    long gives_unheld;        // given while not held
    long take_fails;          // a take that gave up (returned pdFALSE)
    bool contended;           // another task holds it longer than any bounded timeout
    int  scheduler_state;     // what xTaskGetSchedulerState() returns
};
extern SdSimStats sd_sim;
void sd_sim_irq_off(void);
void sd_sim_irq_on(void);
#endif
