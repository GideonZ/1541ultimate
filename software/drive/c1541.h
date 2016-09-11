#ifndef C1541_H
#define C1541_H

#include <stdio.h>
#include "integer.h"
#include "filemanager.h"
#include "file_system.h"
#include "config.h"
#include "disk_image.h"
#include "subsys.h"
#include "menu.h" // to add menu items
#include "flash.h"
#include "iomap.h"
#include "browsable_root.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define C1541_IO_LOC_DRIVE_1 ((volatile uint8_t *)DRIVE_A_BASE)
#define C1541_IO_LOC_DRIVE_2 ((volatile uint8_t *)DRIVE_B_BASE)

#define MENU_1541_RESET     0x1501
#define MENU_1541_REMOVE    0x1502
#define MENU_1541_SAVED64   0x1503
#define MENU_1541_SAVEG64   0x1504
#define MENU_1541_BLANK     0x1505
#define MENU_1541_MOUNT     0x1511
#define MENU_1541_MOUNT_GCR 0x1512
#define MENU_1541_UNLINK    0x1513

#define D64FILE_RUN        0x2101
#define D64FILE_MOUNT      0x2102
#define D64FILE_MOUNT_RO   0x2103
#define D64FILE_MOUNT_UL   0x2104

#define G64FILE_RUN        0x2121
#define G64FILE_MOUNT      0x2122
#define G64FILE_MOUNT_RO   0x2123
#define G64FILE_MOUNT_UL   0x2124

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

class C1541 : public SubSystem, ConfigurableObject, ObjectWithMenu
{
    volatile uint8_t *memory_map;
    volatile uint8_t *registers;
    mstring drive_name;
    FileManager *fm;
    
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

    TaskHandle_t taskHandle;

    void poll();
    static void run(void *a);

    void save_disk_to_file(SubsysCommand *cmd);
    void drive_reset(void);
    void set_hw_address(int addr);
    void set_sw_address(int addr);
    void set_rom(t_1541_rom rom);
    void set_ram(t_1541_ram ram);
    void remove_disk(void);
    void insert_disk(bool protect, GcrImage *image);
    void unlink(void);
    void mount_d64(bool protect, uint8_t *, uint32_t size);
    void mount_d64(bool protect, File *);
    void mount_g64(bool protect, File *);
    void mount_blank(void);
    void check_if_save_needed(SubsysCommand *cmd);
public:
    C1541(volatile uint8_t *regs, char letter);
    ~C1541();

	void init(void);

    int  fetch_task_items(Path *path, IndexedList<Action*> &item_list); // from ObjectWithMenu
    void effectuate_settings(void); // from ConfigurableObject

    // subsys
    const char *identify(void) { return drive_name.c_str(); }
    int  executeCommand(SubsysCommand *cmd);

    // called from IEC (UltiCopy)
    int  get_current_iec_address(void);
    void drive_power(bool on);
};

extern C1541 *c1541_A;
extern C1541 *c1541_B;

#endif

