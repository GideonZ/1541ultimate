#include "size_str.h"
extern "C" {
    #include "small_printf.h"
}

void size_to_string_bytes(DWORD size, char *buffer)
{
    if(size < 1000) {
        small_sprintf(buffer, "%4d ", size);
        return;
    }
    size += 512;
    size >>= 10;

    if(size < 10000) {
        small_sprintf(buffer, "%4dK", size);
        return;
    }
    size += 512;
    size >>= 10;
    small_sprintf(buffer, "%4dM", size);
}

void size_to_string_sectors(DWORD size, char *buffer)
{
    size++;
    size >>= 1;
    
    if(size < 10000) {
        small_sprintf(buffer, "%4d KB", size);
        return;
    }
    size += 512;
    size >>= 10;

    if(size < 10000) {
        small_sprintf(buffer, "%4d MB", size);
        return;
    }
    size += 512;
    size >>= 10;
    small_sprintf(buffer, "%4d GB", size);
}
