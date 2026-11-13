#ifndef PROVIDER_BOOT_H
#define PROVIDER_BOOT_H
#include <stdint.h>
// Call after successful RF startup, before connector/dispatcher startup.
// Repeated Wi-Fi starts retain the identity; zero output stays invalid until reboot.
void provider_boot_init(void);
const uint8_t *provider_boot_identity(void);
#endif
