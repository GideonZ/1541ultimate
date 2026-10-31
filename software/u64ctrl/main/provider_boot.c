#include "provider_boot.h"
#include "provider_identity.h"
#include "esp_random.h"

static uint8_t boot_identity[PROVIDER_ID_SIZE];
static int initialized;
static int valid;

void provider_boot_init(void)
{
    if (initialized) return;
    initialized = 1;
    // Called only after esp_wifi_start succeeds, while RF supplies entropy.
    // Do not borrow the SAR ADC entropy source: its IDF 5.3 cleanup changes
    // the ADC reset/power state shared with voltage sensing and the PHY.
    esp_fill_random(boot_identity, sizeof(boot_identity));
    for (unsigned i = 0; i < sizeof(boot_identity); ++i) valid |= boot_identity[i];
}

const uint8_t *provider_boot_identity(void)
{
    return valid ? boot_identity : 0;
}
