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
#define C1541_IO_LOC_DRIVE_2 ((volatile BYTE *)0x4024000)

#define MENU_1541_RESET     0x1501
#define MENU_1541_REMOVE    0x1502
#define MENU_1541_SAVED64   0x1503
#define MENU_1541_SAVEG64   0x1504
#define MENU_1541_BLANK     0x1505
#define MENU_1541_MOUNT     0x1511
#define MENU_1541_MOUNT_GCR 0x1512
#define MENU_1541_UNLINK    0x1513

struct t_drive_command
{
    int  command;
    File *file;
    bool protect;
};

typedef enum { e_rom_1541=0, e_rom_1541c=1, e_rom_1541ii=2, e_rom_custom=3, e_rom_unset=99 } t_1541_rom;
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

class C1541 : public ConfigurableObject, ObjectWithMenu
{
    volatile BYTE *memory_map;
    volatile BYTE *registers;

    int iec_address;
    char drive_letter;
    bool large_rom;
    t_1541_ram  ram;
	t_1541_rom  current_rom;
	int write_skip;
	
	Flash *flash;
    File *mount_file;
    t_disk_state disk_state;
    GcrImage *gcr_image;
    BinImage *bin_image;
public:
    C1541(volatile BYTE *regs, char letter);
    ~C1541();
    
	void init(void);

    int  fetch_task_items(IndexedList<PathObject*> &item_list); // from ObjectWithMenu
    void effectuate_settings(void); // from ConfigurableObject
    
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
    void mount_blank(void);
    void poll(Event &e);

    void check_if_save_needed(void);
    void save_disk_to_file(bool g64);
};


class DriveMenuItem : public ObjectMenuItem
{
	void *obj;
    t_drive_command *cmd;
public:
	DriveMenuItem(void *o, char *n, int f) : ObjectMenuItem(NULL, n, f), obj(o) {
	}

	~DriveMenuItem() { 
	}

	virtual void execute(int dummy) {
	    cmd = new t_drive_command;
	    cmd->command = function;
	    cmd->file = NULL;
//        cmd->location = location;
	    cmd->protect = false;
		push_event(e_object_private_cmd, obj, (int)cmd);
	}
};


#endif

