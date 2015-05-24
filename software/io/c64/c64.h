#ifndef C64_H
#define C64_H

#include <stdio.h>
#include "host.h"
#include "event.h"
#include "integer.h"
#include "keyboard.h"
#include "screen.h"
#include "config.h"
#include "menu.h"
#include "iomap.h"
#include "filemanager.h"

#define REU_MEMORY_BASE 0x1000000
#define REU_MAX_SIZE    0x1000000

//#define SID_TRACE_END           *((volatile uint32_t *)(C64_TRACE_BASE + 0x80))
//#define SID_REGS(x)             *((volatile uint8_t *)(C64_TRACE_BASE + x))

#define C64_MODE                *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x0))
#define C64_STOP                *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x1))
#define C64_STOP_MODE           *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x2))
#define C64_CLOCK_DETECT        *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x3))
#define C64_CARTRIDGE_RAM_BASE  *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x4))
#define C64_CARTRIDGE_TYPE      *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x5))
#define C64_CARTRIDGE_KILL      *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x6))
#define C64_KERNAL_ENABLE       *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x7))
#define C64_REU_ENABLE          *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x8))
#define C64_REU_SIZE            *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x9))
#define C64_SWAP_CART_BUTTONS   *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xA))
#define C64_TIMING_ADDR_VALID   *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xB))
#define C64_PHI2_EDGE_RECOVER   *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xC))
#define C64_SAMPLER_ENABLE      *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xE))
#define C64_ETHERNET_ENABLE     *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xF))

#define C64_KERNAL_BASE         0x0ECC000

#define C64_MODE_ULTIMAX   0x02
#define C64_MODE_RESET     0x04
#define C64_MODE_UNRESET   0x08
#define C64_MODE_NMI       0x10

#define STOP_COND_BADLINE  0x00
#define STOP_COND_RWSEQ    0x01
#define STOP_COND_FORCE    0x02

#define MODE_NORMAL        0
#define MODE_ULTIMAX       C64_MODE_ULTIMAX

#define C64_DO_STOP        0x01
#define C64_HAS_STOPPED    0x02

#define RUNCODE_LOAD_BIT           0x01
#define RUNCODE_RUN_BIT            0x02
#define RUNCODE_RECORD_BIT         0x10
#define RUNCODE_TAPE_BIT           0x20
#define RUNCODE_REAL_BIT           0x40
#define RUNCODE_MOUNT_BIT          0x80

#define RUNCODE_DMALOAD_RUN       (RUNCODE_LOAD_BIT | RUNCODE_RUN_BIT)
#define RUNCODE_DMALOAD           (RUNCODE_LOAD_BIT)
#define RUNCODE_MOUNT_DMALOAD_RUN  (RUNCODE_MOUNT_BIT | RUNCODE_LOAD_BIT | RUNCODE_RUN_BIT)
#define RUNCODE_MOUNT_LOAD_RUN     (RUNCODE_MOUNT_BIT | RUNCODE_REAL_BIT | RUNCODE_LOAD_BIT | RUNCODE_RUN_BIT)
#define RUNCODE_TAPE_LOAD_RUN     (RUNCODE_TAPE_BIT | RUNCODE_REAL_BIT | RUNCODE_LOAD_BIT | RUNCODE_RUN_BIT)
#define RUNCODE_TAPE_RECORD       (RUNCODE_TAPE_BIT | RUNCODE_RECORD_BIT | RUNCODE_REAL_BIT)

#define VIC_REG(x)   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD000 + x))
#define CIA1_REG(x)  *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC00 + x))
#define CIA2_REG(x)  *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD00 + x))
#define SID_VOLUME   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD418))
#define SID_DUMMY    *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD43F))
#define MEM_LOC(x)   *((volatile uint8_t *)(C64_MEMORY_BASE + 0x0800 + x))
#define COLOR_RAM(x) *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD800 + x))
#define CHAR_DEST(x) *((volatile uint8_t *)(C64_MEMORY_BASE + 0x0800 + x))
#define VIC_CTRL     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD011))
#define BORDER       *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD020))
#define BACKGROUND   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD021))
#define VIC_MMAP     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD018))
#define CIA2_DPA     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD00))
#define CIA2_DDRA    *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD02))
#define CIA1_DPA     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC00))
#define CIA1_DPB     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC01))
#define CIA1_DDRA    *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC02))
#define CIA1_DDRB    *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC03))
#define CIA1_ICR     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC0D))
#define CIA2_ICR     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD0D))
#define C64_SCREEN    (volatile char *)(C64_MEMORY_BASE + 0x0400)
#define C64_COLORRAM  (volatile char *)(C64_MEMORY_BASE + 0xD800)

#define C64_IRQ_SOFT_VECTOR_LO *((volatile uint8_t *)(C64_MEMORY_BASE + 0x0314))
#define C64_IRQ_SOFT_VECTOR_HI *((volatile uint8_t *)(C64_MEMORY_BASE + 0x0315))
#define C64_POKE(x,y) *((volatile uint8_t *)(C64_MEMORY_BASE + x)) = y;
#define C64_PEEK(x)   (*((volatile uint8_t *)(C64_MEMORY_BASE + x)))

#define NUM_VICREGS    48
#define COLOR_SIZE   1024
#define BACKUP_SIZE  2048
#define CHARSET_SIZE 2048

#define CART_REU 0x80 
#define CART_ETH 0x40
#define CART_RAM 0x20

#define C64_EVENT_MAX_REU  0x6477
#define C64_EVENT_AUDIO_ON 0x6478

typedef struct _cart
{
    uint8_t  id;
    void *custom_addr; // dynamically filled in
    uint32_t length;
    uint8_t  type;
} cart_def;


class C64 : public GenericHost, ObjectWithMenu, ConfigurableObject
{
    Flash *flash;
    Keyboard *keyb;
    Screen *screen;
    FileManager *fm;
    
    uint8_t *char_set; //[CHARSET_SIZE];
    uint8_t vic_backup[NUM_VICREGS];
    uint8_t ram_backup[BACKUP_SIZE];
    uint8_t screen_backup[COLOR_SIZE]; // only used now for vic state write
    uint8_t color_backup[COLOR_SIZE];
    uint8_t cia_backup[5];
    
    uint8_t stop_mode;
    uint8_t raster;
    uint8_t raster_hi;
    uint8_t vic_irq_en;
    uint8_t vic_irq;
    uint8_t vic_d011;
    uint8_t vic_d012;

    bool stopped;
    void determine_d012(void);
    void backup_io(void);
    void init_io(void);
    void restore_io(void);
    void set_cartridge(cart_def *def);
    void set_emulation_flags(cart_def *def);

    void stop(bool do_raster = true);
    void resume(void);
public:
    C64();
    ~C64();

    /* Object With Menu */
    int  fetch_task_items(IndexedList<Action *> &item_list);
    static void execute(void *obj, void *action);
    void execute(int command);

    /* Configurable Object */
    void effectuate_settings(void);
    
    /* Generic Host */
    bool exists(void);
    bool is_accessible(void);
    void poll(Event &e);
    void reset(void);
    void freeze(void);
    void unfreeze(Event &e);
    void set_colors(int background, int border);
    Screen *getScreen(void);
    void    releaseScreen(void);
    Keyboard *getKeyboard(void);

    /* C64 specifics */
    void init_cartridge(void);
    void cartridge_test(void);
    int  dma_load(File *f, uint8_t run_mode, uint16_t reloc=0);
    bool write_vic_state(File *f);
        
    friend class FileTypeSID; // sid load does some tricks
};

extern C64   *c64;

class C64Event
{
 public:
    static int prepare_dma_load(File *f, const char *name, int len, uint8_t run_mode, uint16_t reloc=0);
    static int perform_dma_load(File *f, uint8_t run_mode, uint16_t reloc=0);
};

#endif
