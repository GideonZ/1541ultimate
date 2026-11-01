#include <stdio.h>
#include <string.h>
#include "provider_boot.h"
#include "provider_identity.h"
static int fills, failures, zero;
void bootloader_random_enable(void) { ++failures; }
void bootloader_random_disable(void) { ++failures; }
void esp_fill_random(void *bytes, size_t size) {
    if (size != PROVIDER_ID_SIZE) ++failures;
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
    if (fills != 1 || identity != provider_boot_identity()) ++failures;
    printf("Provider boot: %s, %d failures\n", zero ? "zero entropy rejected" : "identity retained without SAR manipulation", failures);
    return failures ? 1 : 0;
}
