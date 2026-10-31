/*
 * radio_entropy_port.h
 *
 * What radio_entropy.c needs from the chip and the RTOS. radio_entropy_esp.c implements
 * it for the modules; the host test implements it with recording stubs.
 */

#ifndef SOFTWARE_WIFI_COMMON_RADIO_ENTROPY_PORT_H_
#define SOFTWARE_WIFI_COMMON_RADIO_ENTROPY_PORT_H_

#include <stdbool.h>
#include <stdint.h>

// A short critical section, never blocking, never held across any other call
void re_port_lock(void);
void re_port_unlock(void);

uint32_t re_port_now_ms(void);
void re_port_delay_ms(uint32_t ms); // at least one RTOS tick
uintptr_t re_port_task(void);       // never 0

void re_port_random_enable(void);   // bootloader_random_enable()
void re_port_random_disable(void);  // bootloader_random_disable()
bool re_port_random_readback(void); // the registers it sets are set

// Before every use of the SAR ADC1 by the voltage read. ESP32-S3: bootloader_random_enable()
// pulses the SAR ADC's reset and leaves its bus clock off, behind the reference count of the
// oneshot ADC driver, which set the clock on when it created its unit and does not set it
// again; this sets it on again. Nothing on the classic ESP32.
void re_port_adc_ready(void);

void re_port_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* SOFTWARE_WIFI_COMMON_RADIO_ENTROPY_PORT_H_ */
