#include "provider_boot.h"
#include "provider_identity.h"
#include "bootloader_random.h"
#include "esp_random.h"

static uint8_t boot_identity[PROVIDER_ID_SIZE];
static int initialized;
static int valid;

void provider_boot_init(void)
{
    if (initialized) return;
    initialized = 1;
    // The internal entropy source must be disabled before configure_adc() or
    // setup_modem(). Identity does not depend on Wi-Fi association or storage.
    bootloader_random_enable();
    esp_fill_random(boot_identity, sizeof(boot_identity));
    bootloader_random_disable();
    for (unsigned i = 0; i < sizeof(boot_identity); ++i) valid |= boot_identity[i];
}

const uint8_t *provider_boot_identity(void)
{
    return valid ? boot_identity : 0;
}
