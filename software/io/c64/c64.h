#ifndef C64_H
#define C64_H

#include <stdio.h>
#include "host.h"
#include "integer.h"
#include "keyboard.h"
#include "screen.h"
#include "config.h"
#include "iomap.h"

#define ROMS_DIRECTORY "/flash/roms"

#define REU_MEMORY_BASE 0x1000000
#define REU_MAX_SIZE    0x1000000

#define MENU_C64_RESET      0x6401
#define MENU_C64_REBOOT     0x6402
#define MENU_C64_SAVEREU    0x6403
#define MENU_C64_SAVEFLASH  0x6404
#define MENU_C64_SAVE_CARTRIDGE 0x6406
#define MENU_C64_BOOTFPGA   0x6408
#define MENU_C64_HARD_BOOT  0x6409
#define MENU_C64_POWEROFF   0x640A
#define MENU_C64_PAUSE      0x640B
#define MENU_C64_RESUME     0x640C
#define MENU_U64_SAVERAM    0x640D
#define MENU_C64_SAVE_MP3_DRV_A 0x640E
#define MENU_C64_SAVE_MP3_DRV_B 0x640F
#define MENU_C64_SAVE_MP3_DRV_C 0x6410
#define MENU_C64_SAVE_MP3_DRV_D 0x6411
#define MENU_MEASURE_TIMING     0x6412
#define MENU_MEASURE_TIMING_API 0x6413
#define MENU_C64_POWERCYCLE     0x6414
#define MENU_C64_CLEARMEM       0x6415

#define C64_DMA_LOAD		0x6464
#define C64_DRIVE_LOAD	    0x6465
#define C64_DMA_LOAD_RAW	0x6466
#define C64_DMA_BUFFER	    0x6467
#define C64_DMA_RAW_WRITE   0x6468
#define C64_DMA_RAW_READ    0x6469
#define C64_DMA_LOAD_MNT    0x646A
#define C64_PUSH_BUTTON     0x6476
#define C64_EVENT_MAX_REU   0x6477
#define C64_EVENT_AUDIO_ON  0x6478
#define C64_START_CART      0x6479
#define C64_UNFREEZE		0x647A
#define C64_STOP_COMMAND	0x647B
#define C64_SET_KERNAL		0x647C
#define C64_READ_FLASH      0x647D

#define C64_MODE                *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x0))
#define C64_STOP                *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x1))
#define C64_STOP_MODE           *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x2))
#define C64_CLOCK_DETECT        *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x3))
#define C64_CARTRIDGE_TYPE      *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x5))
#define C64_CARTRIDGE_KILL      *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x6))
#define C64_CARTRIDGE_ACTIVE    *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x6))
#define C64_KERNAL_ENABLE       *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x7))
#define C64_REU_ENABLE          *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x8))
#define C64_REU_SIZE            *((volatile uint8_t *)(C64_CARTREGS_BASE + 0x9))
#define C64_SWAP_CART_BUTTONS   *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xA))
#define C64_TIMING_ADDR_VALID   *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xB))
#define C64_PHI2_EDGE_RECOVER   *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xC))
#define C64_SERVE_CONTROL       *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xD))
#define C64_SAMPLER_ENABLE      *((volatile uint8_t *)(C64_CARTREGS_BASE + 0xE))

#define C64_MODE_ULTIMAX   0x02
#define C64_MODE_RESET     0x04
#define C64_MODE_UNRESET   0x08
#define C64_MODE_NMI       0x10

#define SERVE_WHILE_STOPPED 0x01

#define STOP_COND_BADLINE  0x00
#define STOP_COND_RWSEQ    0x01
#define STOP_COND_FORCE    0x02

#define MODE_NORMAL        0
#define MODE_ULTIMAX       C64_MODE_ULTIMAX

#define C64_DO_STOP        0x01
#define C64_HAS_STOPPED    0x02

// Bits in CLOCK detect register
#define C64_CD_PHI2_DETECT  0x01
#define C64_CD_VCC_DETECT   0x02
#define C64_CD_EXROM_SENSE  0x04
#define C64_CD_GAME_SENSE   0x08
#define C64_CD_RESET_SENSE  0x10
#define C64_CD_NMI_SENSE    0x20

#define RUNCODE_LOAD_BIT           0x01
#define RUNCODE_RUN_BIT            0x02
#define RUNCODE_JUMP_BIT		   0x04
#define RUNCODE_RECORD_BIT         0x10
#define RUNCODE_TAPE_BIT           0x20
#define RUNCODE_REAL_BIT           0x40
#define RUNCODE_MOUNT_BIT          0x80

#define RUNCODE_DMALOAD_JUMP	  (RUNCODE_LOAD_BIT | RUNCODE_JUMP_BIT)
#define RUNCODE_DMALOAD_RUN       (RUNCODE_LOAD_BIT | RUNCODE_RUN_BIT)
#define RUNCODE_DMALOAD           (RUNCODE_LOAD_BIT)
#define RUNCODE_MOUNT_DMALOAD_RUN  (RUNCODE_MOUNT_BIT | RUNCODE_LOAD_BIT | RUNCODE_RUN_BIT)
#define RUNCODE_MOUNT_LOAD_RUN     (RUNCODE_MOUNT_BIT | RUNCODE_REAL_BIT | RUNCODE_LOAD_BIT | RUNCODE_RUN_BIT)
#define RUNCODE_TAPE_LOAD_RUN     (RUNCODE_TAPE_BIT | RUNCODE_REAL_BIT | RUNCODE_LOAD_BIT | RUNCODE_RUN_BIT)
#define RUNCODE_TAPE_RECORD       (RUNCODE_TAPE_BIT | RUNCODE_RECORD_BIT | RUNCODE_REAL_BIT)

#define FLASH_CMD_PAGESIZE    0x01
#define FLASH_CMD_NOPAGES     0x02
#define FLASH_CMD_GETPAGE     0x03

#define VARIANT_0 0x00
#define VARIANT_1 0x20
#define VARIANT_2 0x40
#define VARIANT_3 0x60
#define VARIANT_4 0x80
#define VARIANT_5 0xA0
#define VARIANT_6 0xC0
#define VARIANT_7 0xE0


#define CART_TYPE_NONE        0x00
#define CART_TYPE_NORMAL      0x01 // variant 0=16K, variant1=Ultimax, variant2=8K, variant3 = off
#define CART_TYPE_EPYX        0x02 // Doesn't have external RAM
#define CART_TYPE_128         0x03
#define CART_TYPE_WMANN       0x04 // 2: Westermann, 1: Blackbox V4
#define CART_TYPE_SBASIC      0x05
#define CART_TYPE_BBASIC      0x06
#define CART_TYPE_BLACKBOX_V3 0x07

#define CART_TYPE_OCEAN_8K    0x08 // 0: ocean, 1: domark, 2: gmod2
#define CART_TYPE_OCEAN_16K   0x09 // 0: 4 banks, 1: 8 banks, 3: 16 banks, 7: 32 banks
#define CART_TYPE_SYSTEM3     0x0A // Doesn't have external RAM
#define CART_TYPE_SUPERGAMES  0x0B
#define CART_TYPE_BLACKBOX_V8 0x0C
#define CART_TYPE_ZAXXON      0x0D
#define CART_TYPE_BLACKBOX_V9 0x0E

#define CART_TYPE_PAGEFOX     0x10
#define CART_TYPE_EASY_FLASH  0x11 // ?

#define CART_TYPE_FINAL12     0x18
#define CART_TYPE_FC3         0x19 // 0: 64K, 1: 256K
#define CART_TYPE_SS5         0x1A
#define CART_TYPE_ACTION      0x1B // 0: AR, 1: RR, 2: Nordic
#define CART_TYPE_KCS         0x1C
#define CART_TYPE_GEORAM      0x1F

// predefined combinations
#define CART_TYPE_8K          (CART_TYPE_NORMAL | VARIANT_2)
#define CART_TYPE_16K         (CART_TYPE_NORMAL | VARIANT_0)
#define CART_TYPE_UMAX        (CART_TYPE_NORMAL | VARIANT_5) // 1 = Umax, 5 = Umax + serve VIC
#define CART_TYPE_BLACKBOX_V4 (CART_TYPE_WMANN | VARIANT_1)
#define CART_TYPE_WESTERMANN  (CART_TYPE_WMANN | VARIANT_2)
#define CART_TYPE_DOMARK      (CART_TYPE_OCEAN_8K | VARIANT_1)
#define CART_TYPE_GMOD2       (CART_TYPE_OCEAN_8K | VARIANT_2)
#define CART_TYPE_RETRO       (CART_TYPE_ACTION | VARIANT_1)
#define CART_TYPE_NORDIC      (CART_TYPE_ACTION | VARIANT_2)
#define CART_TYPE_FC3PLUS     (CART_TYPE_FC3 | VARIANT_1)

#define DRVTYPE_MP3_DNP       31

#define VIC_REG(x)   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD000 + x))
#define CIA1_REG(x)  *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC00 + x))
#define CIA2_REG(x)  *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD00 + x))
#define CIA1_CRA     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC0E))
#define CIA1_CRB     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDC0F))
#define SID_VOLUME   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD418))
#define SID2_VOLUME  *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD438))
#define SID3_VOLUME  *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD518))
#define SID_DUMMY    *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD41F))
#define SID2_DUMMY   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD43F))
#define SID3_DUMMY   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD51F))
#define VIC_CTRL     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD011))
#define BORDER       *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD020))
#define BACKGROUND   *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD021))
#define VIC_MMAP     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xD018))
#define CIA2_DPA     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD00))
#define CIA2_DPB     *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD01))
#define CIA2_DDRA    *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD02))
#define CIA2_DDRB    *((volatile uint8_t *)(C64_MEMORY_BASE + 0xDD03))
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

#define NUM_VICREGS    49
#define COLOR_SIZE   1024
#define BACKUP_SIZE  2048
#define CHARSET_SIZE 2048

#define CFG_C64_REU_EN      0xC3
#define CFG_C64_REU_SIZE    0xC4
#define CFG_C64_ETH_EN      0xC5
#define CFG_C64_SWAP_BTN    0xC6
#define CFG_C64_DMA_ID      0xC7
#define CFG_C64_MAP_SAMP    0xC8
#define CFG_C64_TIMING      0xCB
#define CFG_C64_PHI2_REC    0xCC
#define CFG_C64_RATE        0xCD
#define CFG_C64_CART_PREF   0xCE
#define CFG_C64_TIMING1     0xCF
#define CFG_SERVE_PHI1      0xD0
#define CFG_MEASURE_MODE    0xD1
#define CFG_KERNAL_SHADOW   0xD2
#define CFG_CMD_ENABLE      0x71
#define CFG_CMD_ALLOW_WRITE 0x72
#define CFG_C64_FASTRESET   0x74
#define CFG_C64_DO_SYNC     0x75
#define CFG_C64_REU_PRE     0x80
#define CFG_C64_REU_IMG     0x81
#define CFG_C64_REU_OFFS    0x82

#define CFG_C64_KERNFILE    0xE1
#define CFG_C64_BASIFILE    0xE2
#define CFG_C64_CHARFILE    0xE3
#define CFG_C64_CART_CRT    0xE4

#define CFG_BUS_MODE          0x4D
#define CFG_BUS_SHARING_ROM   0x4E
#define CFG_BUS_SHARING_IO1   0x4F
#define CFG_BUS_SHARING_IRQ   0x50
#define CFG_BUS_SHARING_IO2   0x51

#define CART_ACIA_DE  0x001
#define CART_ACIA_DF  0x002
#define CART_REU      0x004
#define CART_MAXREU   0x008
#define CART_UCI      0x010
#define CART_UCI_DFFC 0x020
#define CART_UCI_DE1C 0x040
#define CART_SAMPLER  0x080
#define CART_WMIRROR  0x100 // U64 bit: this cartridge requires all writes to be mirrored
#define CART_DYNAMIC  0x200 // U64 bit: this cartridge requires dynamic bus optimization
#define CART_KERNAL   0x400 // special bit that requires the driver to copy the cartridge ROM data to kernal emulation space

#define CART_PROHIBIT_DEXX (CART_ACIA_DE)
#define CART_PROHIBIT_DFXX (CART_ACIA_DF | CART_REU | CART_MAXREU | CART_UCI | CART_SAMPLER)
#define CART_PROHIBIT_IO   (CART_PROHIBIT_DEXX | CART_PROHIBIT_DFXX)
#define CART_PROHIBIT_ALL_BUT_REU (CART_PROHIBIT_DEXX | CART_ACIA_DF | CART_SAMPLER)
#define CART_PROHIBIT_ALL_BUT_REU_AND_ACIA_DE (CART_ACIA_DF | CART_SAMPLER)


typedef struct _cart
{
    const char *name;
    mstring filename;
    void *custom_addr; // used for precompiled ROMs that are not part of a CRT (yet)
    uint32_t length;   // used for precompiled ROMs that are not part of a CRT (yet)
    uint16_t type;     // raw cart type, which maps on hardware ID
    uint16_t prohibit; // flags that tell us what other I/O modules can be switched on
    uint16_t require;  // features that need to be turned on for this module to work.
    uint16_t disabled; // Tells us which features got disabled during configuration (I/O conflict)
} cart_def;


class C64 : public GenericHost, ConfigurableObject
{
    cart_def current_cart_def;

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
    bool backupIsValid;

    volatile bool buttonPushSeen;
    volatile bool available;

    bool isFrozen;
    void determine_d012(void);
    void goUltimax(void);
    void backup_io(void);
    void init_io(void);
    void restore_io(void);
    void set_cartridge(cart_def *def);
    void set_emulation_flags();
    void disable_kernal();
    void init_system_roms(void);

    void stop(bool do_raster);
    static void hard_stop(void);
    void resume(void);
    void freeze(void);
    void measure_timing(uint8_t *buffer);
    virtual void get_all_memory(uint8_t *) { /* NOT YET IMPLEMENTED */ };
    virtual void clear_ram(void) { /* NOT YET IMPLEMENTED */ };
    static uint8_t get_exrom_game(void) {
        return (C64_CLOCK_DETECT & 0x0C) >> 2;
    }
    static bool phi2_present(void) {
        return (C64_CLOCK_DETECT & C64_CD_PHI2_DETECT) == C64_CD_PHI2_DETECT;
    }
    static bool powered_by_c64(void) {
        return (C64_CLOCK_DETECT & C64_CD_VCC_DETECT) == C64_CD_VCC_DETECT;
    }
    static bool c64_reset_detect(void) {
        return (C64_CLOCK_DETECT & C64_CD_RESET_SENSE) == C64_CD_RESET_SENSE;
    }
    static void init_poll_task(void *a);
    static int setCartPref(ConfigItem *item);

#if U64
    bool ConfigureU64SystemBus(void);
    void EnableWriteMirroring(void);
#endif

    C64();
public:
    void init(void);
    virtual ~C64();

    /* Get static object */
    static C64 *getMachine(void);

    /* Configurable Object */
    void effectuate_settings(void);
    
    /* Generic Host */
    void take_ownership(HostClient *client) {
    	this->client = client;
    	freeze();
    }
    void release_ownership(void) {
    	unfreeze();
    	this->client = 0;
    }

    void checkButton(void);
    bool hasButton(void) {
    	return true;
    }
    bool buttonPush(void);
    void setButtonPushed(void);

    bool exists(void);
    bool is_accessible(void);
    bool is_stopped(void);
    
    void set_colors(int background, int border);
    Screen *getScreen(void);
    Keyboard *getKeyboard(void);

    int  get_cfg_value(uint8_t id)
    {
       if (!cfg) return 0;
       return cfg->get_value(id);	
    }
    
    /* C64 specifics */
    void resetConfigInFlash(int page);
    void unfreeze(void);
    void start_cartridge(void *def);
    void enable_kernal(uint8_t *rom);
    void set_rom_config(uint8_t idx, const char *fname);
    void init_cartridge(void);
    void reset(void);
    void start(void);
    bool is_in_reset(void);

    static void clear_cart_definition(cart_def *def) {
        def->custom_addr = 0;
        def->length = 0;
        def->require = 0;
        def->prohibit = 0;
        def->disabled = 0;
        def->filename = "";
        def->type = CART_TYPE_NONE;
        def->name = "None";
    }
    const cart_def *get_cart_definition() { return &current_cart_def; } // Dangerous!

    static int isMP3RamDrive(int dev);
    static int getSizeOfMP3NativeRamdrive(int dev);

    static uint8_t *get_cartridge_rom_addr(void) {
        extern uint8_t __cart_rom_start[1024*1024];
        return __cart_rom_start;
    }

    static uint8_t *get_cartridge_ram_addr(void) {
        extern uint8_t __cart_ram_start[64*1024];
        return __cart_ram_start;
    }

    static void get_eeprom_data(uint8_t *buffer);
    static void set_eeprom_data(uint8_t *buffer);
    static bool get_eeprom_dirty(void);

    static bool c64_get_nmi_state(void) {
        return (C64_CLOCK_DETECT & C64_CD_NMI_SENSE) == C64_CD_NMI_SENSE;
    }

    static void list_crts(ConfigItem *it, IndexedList<char *>& strings);
    static void list_kernals(ConfigItem *it, IndexedList<char *>& strings);
    static void list_basics(ConfigItem *it, IndexedList<char *>& strings);
    static void list_chars(ConfigItem *it, IndexedList<char *>& strings);

    friend class FileTypeSID; // sid load does some tricks
    friend class C64_Subsys; // the wrapper with file access
    friend class REUPreloader; // preloader needs to access config
    friend class FileTypeREU; // REU file needs to access config
    friend class FileTypeCRT; // CRT file may need to enable Write mirroring on U64
    friend class U64Machine;  // U64Machine is a derived class that may use internals of C64
    friend class U64Config; // U64 config needs to stop / resume for SID detection
    friend class SoftIECTarget; // UCI target that performs DMA load
};

// extern C64 *c64;

#endif
