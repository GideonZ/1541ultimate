#ifndef C64_H
#define C64_H

#include <stdio.h>
#include "host.h"
#include "integer.h"
#include "keyboard.h"
#include "screen.h"
#include "config.h"
#include "iomap.h"

#define REU_MEMORY_BASE 0x1000000
#define REU_MAX_SIZE    0x1000000

#define MENU_C64_RESET      0x6401
#define MENU_C64_REBOOT     0x6402
//#define MENU_C64_TRACE    0x6581
#define MENU_C64_SAVEREU    0x6403
#define MENU_C64_SAVEFLASH  0x6404
#define MENU_C64_BOOTFPGA   0x6408
#define MENU_C64_HARD_BOOT  0x6409
#define C64_DMA_LOAD		0x6464
#define C64_DRIVE_LOAD	    0x6465
#define C64_DMA_LOAD_RAW	0x6466
#define C64_EVENT_MAX_REU   0x6477
#define C64_EVENT_AUDIO_ON  0x6478
#define C64_START_CART      0x6479
#define C64_UNFREEZE		0x647A

//#define SID_TRACE_END           *((volatile uint32_t *)(C64_TRACE_BASE + 0x80))
//#define SID_REGS(x)             *((volatile uint8_t *)(C64_TRACE_BASE + x))

#define C64_MODE                *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x0))
#define C64_STOP                *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x1))
#define C64_STOP_MODE           *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x2))
#define C64_CLOCK_DETECT        *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x3))
#define C64_CARTRIDGE_RAM_BASE  *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x4))
#define C64_CARTRIDGE_TYPE      *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x5))
#define C64_CARTRIDGE_KILL      *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x6))
#define C64_CARTRIDGE_ACTIVE    *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x6))
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

#define CART_TYPE_NONE        0x00
#define CART_TYPE_8K          0x01
#define CART_TYPE_16K         0x02
#define CART_TYPE_16K_UMAX    0x03
#define CART_TYPE_FC3         0x04
#define CART_TYPE_SS5         0x05
#define CART_TYPE_RETRO       0x06
#define CART_TYPE_ACTION      0x07
#define CART_TYPE_SYSTEM3     0x08
#define CART_TYPE_DOMARK      0x09
#define CART_TYPE_OCEAN128    0x0A
#define CART_TYPE_OCEAN256    0x0B
#define CART_TYPE_EASY_FLASH  0x0C
#define CART_TYPE_EPYX        0x0E

#define VIC_REG(x)   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD000 + x))
#define CIA1_REG(x)  *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC00 + x))
#define CIA2_REG(x)  *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD00 + x))
#define CIA1_CRA     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC0E))
#define CIA1_CRB     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC0F))
#define SID_VOLUME   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD418))
#define SID_DUMMY    *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD43F))
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

#define MEM_LOC       ((volatile uint32_t *)(C64_MEMORY_BASE + 0x0800))
#define SCREEN_RAM    ((volatile uint32_t *)(C64_MEMORY_BASE + 0x0400))
#define COLOR_RAM     ((volatile uint32_t *)(C64_MEMORY_BASE + 0xD800))
#define CHAR_DEST     ((volatile uint8_t *)(C64_MEMORY_BASE + 0x0800))

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

#define CFG_C64_CART     0xC1
#define CFG_C64_CUSTOM   0xC2
#define CFG_C64_REU_EN   0xC3
#define CFG_C64_REU_SIZE 0xC4
#define CFG_C64_ETH_EN   0xC5
#define CFG_C64_SWAP_BTN 0xC6
#define CFG_C64_DMA_ID   0xC7
#define CFG_C64_MAP_SAMP 0xC8
#define CFG_C64_ALT_KERN 0xC9
#define CFG_C64_KERNFILE 0xCA
#define CFG_C64_TIMING   0xCB
#define CFG_C64_PHI2_REC 0xCC
#define CFG_C64_RATE	 0xCD
#define CFG_CMD_ENABLE   0x71

typedef struct _cart
{
    uint8_t  id;
    void *custom_addr; // dynamically filled in
    uint32_t length;
    uint8_t  type;
} cart_def;


class C64 : public GenericHost, ConfigurableObject
{
    HostClient *client;
    Flash *flash;
    Keyboard *keyb;
    Screen *screen;
    
    uint8_t *char_set; //[CHARSET_SIZE];
    uint8_t vic_backup[NUM_VICREGS];
    uint32_t ram_backup[BACKUP_SIZE/4];
    uint32_t screen_backup[COLOR_SIZE/4]; // only used now for vic state write
    uint32_t color_backup[COLOR_SIZE/4];
    uint8_t cia_backup[8];
    
    uint8_t stop_mode;
    uint8_t raster;
    uint8_t raster_hi;
    uint8_t vic_irq_en;
    uint8_t vic_irq;
    uint8_t vic_d011;
    uint8_t vic_d012;

    volatile bool buttonPushSeen;

    bool stopped;
    void determine_d012(void);
    void backup_io(void);
    void init_io(void);
    void restore_io(void);
    void set_cartridge(cart_def *def);
    void set_emulation_flags(cart_def *def);

    void stop(bool do_raster = true);
    void resume(void);

    void freeze(void);

public:
    C64();
    ~C64();

    /* Configurable Object */
    void effectuate_settings(void);
    
    /* Generic Host */
    void take_ownership(HostClient *client) {
    	this->client = client;
    	freeze();
    }
    void release_ownership(void) {
    	unfreeze(0, 0); // continue where we left off
    	this->client = 0;
    }
    bool hasButton(void) {
    	return true;
    }
    bool buttonPush(void);
    void setButtonPushed(void);

    bool exists(void);
    bool is_accessible(void);

    void set_colors(int background, int border);
    Screen *getScreen(void);
    void    releaseScreen(void);
    Keyboard *getKeyboard(void);

    /* C64 specifics */
    void unfreeze(cart_def *def, int mode);  // called from crt... hmm FIXME

    void init_cartridge(void);
    void cartridge_test(void);
    void reset(void);
        
    friend class FileTypeSID; // sid load does some tricks
    friend class C64_Subsys; // the wrapper with file access
};

extern C64 *c64;

#endif
