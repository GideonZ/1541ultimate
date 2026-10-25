#ifndef PROVIDER_BOOT_H
#define PROVIDER_BOOT_H
#include <stdint.h>
// Call exactly once before ADC/RF initialization and dispatcher startup.
void provider_boot_init(void);
const uint8_t *provider_boot_identity(void);
#endif
