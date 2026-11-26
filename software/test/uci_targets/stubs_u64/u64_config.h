// The part of U64Config that control_target.cc calls (palette commands); not exercised.
#ifndef UCI_STUB_U64_CONFIG_H
#define UCI_STUB_U64_CONFIG_H
#include <stdint.h>
class U64Config {
public:
    static void get_palette_rgb(uint8_t (*)[3]) { }
    static bool set_palette_rgb(uint8_t (*)[3]) { return true; }
    static bool set_palette_color(int, const uint8_t *) { return true; }
    static void reset_palette(void) { }
};
#endif
