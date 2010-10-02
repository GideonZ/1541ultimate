#ifndef C64_H
#define C64_H

#include "event.h"
#include "integer.h"
#include "keyboard.h"
#include "config.h"
#include "filemanager.h"

#define C64_CARTREGS_BASE 0x4040000
#define C64_MEMORY_BASE   0x5050000 // never corrected for endianness

#define REU_MEMORY_BASE 0x1000000
#define REU_MAX_SIZE    0x1000000

#define C64_MODE                *((volatile BYTE *)(C64_CARTREGS_BASE + 0x0))
#define C64_STOP                *((volatile BYTE *)(C64_CARTREGS_BASE + 0x1))
#define C64_STOP_MODE           *((volatile BYTE *)(C64_CARTREGS_BASE + 0x2))
#define C64_CLOCK_DETECT        *((volatile BYTE *)(C64_CARTREGS_BASE + 0x3))
#define C64_CARTRIDGE_RAM_BASE  *((volatile BYTE *)(C64_CARTREGS_BASE + 0x4))
#define C64_CARTRIDGE_TYPE      *((volatile BYTE *)(C64_CARTREGS_BASE + 0x5))
#define C64_CARTRIDGE_KILL      *((volatile BYTE *)(C64_CARTREGS_BASE + 0x6))
#define C64_REU_ENABLE          *((volatile BYTE *)(C64_CARTREGS_BASE + 0x8))
#define C64_REU_SIZE            *((volatile BYTE *)(C64_CARTREGS_BASE + 0x9))
#define C64_SWAP_CART_BUTTONS   *((volatile BYTE *)(C64_CARTREGS_BASE + 0xA))
#define C64_ETHERNET_ENABLE     *((volatile BYTE *)(C64_CARTREGS_BASE + 0xF))

#define C64_MODE_EXROM     0x01
#define C64_MODE_GAME      0x02
#define C64_MODE_RESET     0x04
#define C64_MODE_UNRESET   0x08
#define C64_MODE_NMI       0x10

#define STOP_COND_BADLINE  0x00
#define STOP_COND_RWSEQ    0x01
#define STOP_COND_FORCE    0x02

#define MODE_NORMAL        0
#define MODE_ULTIMAX       C64_MODE_GAME

#define C64_DO_STOP        0x01
#define C64_HAS_STOPPED    0x02

#define DMA_RUN    0xBC
#define DMA_BASIC  0x80

#define VIC_REG(x)   *((volatile BYTE *)(C64_MEMORY_BASE + 0xD000 + x))
#define CIA1_REG(x)  *((volatile BYTE *)(C64_MEMORY_BASE + 0xDC00 + x))
#define CIA2_REG(x)  *((volatile BYTE *)(C64_MEMORY_BASE + 0xDD00 + x))
#define SID_VOLUME   *((volatile BYTE *)(C64_MEMORY_BASE + 0xD418))
#define SID_DUMMY    *((volatile BYTE *)(C64_MEMORY_BASE + 0xD43F))
#define MEM_LOC(x)   *((volatile BYTE *)(C64_MEMORY_BASE + 0x0800 + x))
#define COLOR_RAM(x) *((volatile BYTE *)(C64_MEMORY_BASE + 0xD800 + x))
#define CHAR_DEST(x) *((volatile BYTE *)(C64_MEMORY_BASE + 0x0800 + x))
#define VIC_CTRL     *((volatile BYTE *)(C64_MEMORY_BASE + 0xD011))
#define BORDER       *((volatile BYTE *)(C64_MEMORY_BASE + 0xD020))
#define BACKGROUND   *((volatile BYTE *)(C64_MEMORY_BASE + 0xD021))
#define VIC_MMAP     *((volatile BYTE *)(C64_MEMORY_BASE + 0xD018))
#define CIA2_DPA     *((volatile BYTE *)(C64_MEMORY_BASE + 0xDD00))
#define CIA2_DDRA    *((volatile BYTE *)(C64_MEMORY_BASE + 0xDD02))
#define CIA1_DPA     *((volatile BYTE *)(C64_MEMORY_BASE + 0xDC00))
#define CIA1_DPB     *((volatile BYTE *)(C64_MEMORY_BASE + 0xDC01))
#define CIA1_DDRA    *((volatile BYTE *)(C64_MEMORY_BASE + 0xDC02))
#define CIA1_DDRB    *((volatile BYTE *)(C64_MEMORY_BASE + 0xDC03))
#define CIA1_ICR     *((volatile BYTE *)(C64_MEMORY_BASE + 0xDC0D))
#define CIA2_ICR     *((volatile BYTE *)(C64_MEMORY_BASE + 0xDD0D))
#define C64_SCREEN    (volatile char *)(C64_MEMORY_BASE + 0x0400)
#define C64_COLORRAM  (volatile char *)(C64_MEMORY_BASE + 0xD800)

#define C64_IRQ_SOFT_VECTOR_LO *((volatile BYTE *)(C64_MEMORY_BASE + 0x0314))
#define C64_IRQ_SOFT_VECTOR_HI *((volatile BYTE *)(C64_MEMORY_BASE + 0x0315))
#define C64_POKE(x,y) *((volatile BYTE *)(C64_MEMORY_BASE + x)) = y;
#define C64_PEEK(x)   (*((volatile BYTE *)(C64_MEMORY_BASE + x)))

#define NUM_VICREGS    48
#define COLOR_SIZE   1000
#define BACKUP_SIZE  2048
#define CHARSET_SIZE 2048

#define CART_REU 0x80 
#define CART_ETH 0x40
#define CART_RAM 0x20

typedef struct _cart
{
    BYTE  id;
    void *custom_addr; // dynamically filled in
    DWORD length;
    BYTE  type;
} cart_def;

class Keyboard;

class C64
{
    Flash *flash;
    Keyboard *keyb;
    ConfigStore *cfg;
    
    BYTE *char_set; //[CHARSET_SIZE];
    BYTE vic_backup[NUM_VICREGS];
    BYTE ram_backup[BACKUP_SIZE];
    BYTE cia_backup[5];
    
    BYTE stop_mode;
    BYTE raster;
    BYTE raster_hi;
    BYTE vic_irq_en;
    BYTE vic_irq;
    BYTE vic_d011;
    BYTE vic_d012;

    bool stopped;
    void determine_d012(void);
    void backup_io(void);
    void init_io(void);
    void restore_io(void);
    void set_cartridge(cart_def *def);

    void stop(bool do_raster = true);
    void resume(void);
public:
    C64();
    ~C64();

    void create_menu_items(void);

    bool exists(void);
    void poll(Event &e);
    void reset(void);
    void freeze(void);
    void unfreeze(int, cart_def *);
    char *get_screen(void);
    char *get_color_map(void);
    Keyboard *get_keyboard(void);
    void init_cartridge(void);

    void cartridge_test(void);
    int  dma_load(File *f, BYTE run_mode, WORD reloc=0);
    
    // status
    bool has_stopped(void);
    
    friend class FileTypeSID; // sid load does some tricks
};

extern C64   *c64;

#endif
