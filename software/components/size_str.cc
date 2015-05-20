#include "../components/size_str.h"

#include <stdio.h>
#include <string.h>

void size_to_string_bytes(uint32_t size, char *buffer)
{
    if(size < 1000) {
        sprintf(buffer, "%4d ", size);
        return;
    }
    size += 512;
    size >>= 10;

    if(size < 10000) {
        sprintf(buffer, "%4dK", size);
        return;
    }
    size += 512;
    size >>= 10;
    sprintf(buffer, "%4dM", size);
}

void size_to_string_sectors(uint32_t size, char *buffer)
{
    size++;
    size >>= 1;
    
    if(size < 10000) {
        sprintf(buffer, "%4d KB", size);
        return;
    }
    size += 512;
    size >>= 10;

    if(size < 10000) {
        sprintf(buffer, "%4d MB", size);
        return;
    }
    size += 512;
    size >>= 10;
    sprintf(buffer, "%4d GB", size);
}
