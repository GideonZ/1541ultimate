#include <stdio.h>
#include <string.h>
#include "provider_boot.h"
#include "provider_identity.h"
static int enabled, fills, disables, failures, zero;
void bootloader_random_enable(void) { if (enabled || fills) ++failures; enabled = 1; }
void bootloader_random_disable(void) { if (!enabled || fills != 1) ++failures; enabled = 0; ++disables; }
void esp_fill_random(void *bytes, size_t size) {
    if (!enabled || size != PROVIDER_ID_SIZE) ++failures;
    memset(bytes, zero ? 0 : 0x5a, size); ++fills;
}
int main(int argc, char **argv)
{
    zero = argc > 1 && !strcmp(argv[1], "--zero");
    if (provider_boot_identity()) ++failures;
    provider_boot_init();
    const unsigned char *identity = provider_boot_identity();
    if (zero ? identity != NULL : identity == NULL) ++failures;
    if (identity && (identity[0] != 0x5a || identity[15] != 0x5a)) ++failures;
    provider_boot_init();
    if (fills != 1 || disables != 1 || enabled || identity != provider_boot_identity()) ++failures;
    printf("Provider boot: %s, %d failures\n", zero ? "zero entropy rejected" : "qualified startup order", failures);
    return failures ? 1 : 0;
}
