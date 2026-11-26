// The part of class C64 (io/c64/c64.h) that U64Machine's memory functions use.
#ifndef C64_H
#define C64_H
#include <stdint.h>
#include <string.h>
// the window macros of io/c64/c64.h; the test defines its own model of them before including this
#ifndef C64_PEEK
#define C64_POKE(x,y) *((volatile uint8_t *)(C64_MEMORY_BASE + x)) = y;
#define C64_PEEK(x)   (*((volatile uint8_t *)(C64_MEMORY_BASE + x)))
#endif
#define COLOR_SIZE   1024
#define BACKUP_SIZE  2048
class U64Machine;
class C64
{
public:
    bool isFrozen;
    uint32_t ram_backup[BACKUP_SIZE/4];
    uint32_t screen_backup[COLOR_SIZE/4];
    uint32_t color_backup[COLOR_SIZE/4];
    C64() : isFrozen(false) { }
    void stop(bool do_raster);
    void resume(void);
    bool is_stopped(void);
    // U64Machine's constructor and get_all_memory are private; C64 is its friend
    static U64Machine *make_u64(void);
    static void get_all_memory_of(U64Machine *m, uint8_t *pb);
};
#endif
