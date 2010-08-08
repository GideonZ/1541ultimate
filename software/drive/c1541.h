#ifndef C1541_H
#define C1541_H

#include "integer.h"
#include "event.h"
#include "file_system.h"
#include "config.h"
#include "disk_image.h"
#include "menu.h" // to add menu items
#include "flash.h"

#define C1541_IO_LOC_DRIVE_1 ((volatile BYTE *)0x4020000)


typedef enum { e_rom_1541=0, e_rom_1541c=1, e_rom_1541ii=2, e_rom_custom=3 } t_1541_rom;
typedef enum { e_ram_none      = 0x00,
               e_ram_8000_BFFF = 0x30,
               e_ram_4000_7FFF = 0x0C,
               e_ram_4000_BFFF = 0x3C,
               e_ram_2000_3FFF = 0x02,
               e_ram_2000_7FFF = 0x0E,
               e_ram_2000_BFFF = 0x3E } t_1541_ram;

typedef enum { e_no_disk,
               e_alien_image,
               e_disk_file_closed,
               e_d64_disk,
               e_gcr_disk } t_disk_state;

// register offsets
#define C1541_POWER       0
#define C1541_RESET       1
#define C1541_HW_ADDR     2
#define C1541_SENSOR      3
#define C1541_INSERTED    4
#define C1541_RAMMAP      5
#define C1541_ANYDIRTY    6
#define C1541_DIRTYIRQ    7
#define C1541_TRACK       8
#define C1541_STATUS      9
#define C1541_MEM_ADDR   10
#define C1541_AUDIO_ADDR 11

#define C1541_DIRTYFLAGS    0x0800
#define C1541_PARAM_RAM     0x1000

// constants
#define SENSOR_DARK  0
#define SENSOR_LIGHT 1

#define DRVSTAT_MOTOR   0x01
#define DRVSTAT_WRITING 0x02

class C1541
{
    volatile BYTE *memory_map;
    volatile BYTE *registers;
    int iec_address;

    bool large_rom;
    t_1541_ram  ram;
	t_1541_rom  current_rom;
	
	Flash *flash;
    ConfigStore *cfg;
    File *mount_file;
    t_disk_state disk_state;
    GcrImage *gcr_image;
    BinImage *bin_image;
public:
    C1541(volatile BYTE *regs);
    ~C1541();
    
    void create_menu_items(void);

    void drive_power(bool on);
    void drive_reset(void);
    void set_hw_address(int addr);
    void set_sw_address(int addr);
    int  get_current_iec_address(void);
    void set_rom(t_1541_rom rom, char *);
    void set_ram(t_1541_ram ram);
    void remove_disk(void);
    void insert_disk(bool protect, GcrImage *image);
    void mount_d64(bool protect, File *);
    void mount_g64(bool protect, File *);
    void poll(Event &e);
};

extern C1541 *c1541; // default, the first drive!

#endif

