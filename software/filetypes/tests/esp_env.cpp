#include "esp_env.h"

EspWorld world;
Esp32 esp32;

void reset_world()
{
    world = EspWorld();
    world.reported_size = 0;
    world.download_status = 0;
    world.flash_status = 0;
    world.verify_mode = EspWorld::VERIFY_GOOD36;
    world.verifies = 0;
    world.opens = world.closes = 0;
    world.live_bytes = 0;
}

void set_image(const std::vector<uint8_t> &img)
{
    world.content = img;
    world.reported_size = img.size();
}

// A count of the bytes new[] hands out and delete[] takes back, to find a buffer that is
// never freed (the sizes are kept in a header ahead of each block)
void *operator new[](size_t n)
{
    char *p = (char *)malloc(n + 16);
    *(size_t *)p = n;
    world.live_bytes += n;
    return p + 16;
}
void operator delete[](void *q) noexcept
{
    if (!q) return;
    char *p = (char *)q - 16;
    world.live_bytes -= *(size_t *)p;
    free(p);
}
void operator delete[](void *q, size_t) noexcept { operator delete[](q); }
