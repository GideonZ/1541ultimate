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
#include "wd177x.h"

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
#define MENU_1541_TURNON    0x1506
#define MENU_1541_TURNOFF   0x1507
#define FLOPPY_LOAD_DOS     0x1508
#define MENU_1541_UNLINK    0x1513
#define MENU_1541_SWAP      0x1514
#define MENU_1541_SET_MODE  0x1515

#define MENU_1541_READ_ONLY    0x8000
#define MENU_1541_UNLINKED     0x4000
#define MENU_1541_NO_FLAGS     0x1FFF

#define MENU_1541_MOUNT_D64    0x1520 // +1 for .d71, +2 for .d81
#define MENU_1541_MOUNT_D64_RO (MENU_1541_MOUNT_D64 | MENU_1541_READ_ONLY)
#define MENU_1541_MOUNT_D64_UL (MENU_1541_MOUNT_D64 | MENU_1541_UNLINKED)

#define MENU_1541_MOUNT_G64    0x1530
#define MENU_1541_MOUNT_G64_RO (MENU_1541_MOUNT_G64 | MENU_1541_READ_ONLY)
#define MENU_1541_MOUNT_G64_UL (MENU_1541_MOUNT_G64 | MENU_1541_UNLINKED)


typedef enum { e_no_disk,
               e_alien_image,
               e_disk_file_closed,
               e_d64_disk,
               e_d81_disk,
               e_gcr_disk } t_disk_state;

typedef enum { e_dt_1541 = 0,
               e_dt_1571 = 1,
               e_dt_1581 = 2,
               e_dt_unset = 3,
} t_drive_type;

// register offsets
#define C1541_POWER       0
#define C1541_RESET       1
#define C1541_HW_ADDR     2
#define C1541_SENSOR      3
#define C1541_INSERTED    4
#define C1541_RAMMAP      5
#define C1541_SIDE        6
#define C1541_MAN_WRITE   7
#define C1541_TRACK       8
#define C1541_STATUS      9
#define C1541_DISKCHANGE 12
#define C1541_DRIVETYPE  13
#define C1541_SOUNDS     14

#define C1541_DIRTYFLAGS    0x0800
#define C1541_PARAM_RAM     0x1000
#define C1541_WD177X        0x1800

// constants
#define SENSOR_DARK  0
#define SENSOR_LIGHT 1

#define DRVSTAT_MOTOR      0x01
#define DRVSTAT_WRITING    0x02
#define DRVSTAT_WRITEBUSY  0x04

class C1541 : public SubSystem, ConfigurableObject, ObjectWithMenu
{
    static C1541* last_mounted_drive;
    volatile uint8_t *memory_map;
    volatile uint8_t *registers;
    uint8_t *audio_address;

    t_cfg_definition *local_config_definitions;
    mstring drive_name;
    t_drive_type current_drive_type;
    mstring current_drive_rom;
    FileManager *fm;
    
    TaskCategory *taskItemCategory;
    int iec_address;
    char drive_letter;
    bool multi_mode;
    int write_skip;

	uint32_t mfm_dirty_bits[2][3]; // two sides, max 96 tracks each.
	
	Flash *flash;
	mstring mount_file_path;
	mstring mount_file_name;
	int mount_function_id;
	int mount_mode;
	File *mount_file;
    File *gcr_ram_file;
    t_disk_state disk_state;
    bool gcr_image_up_to_date;

    GcrImage *gcr_image;
    BinImage *bin_image;
    uint8_t *dummy_track;
    WD177x *mfm_controller;

    TaskHandle_t taskHandle;

    struct {
        Action *reset;
        Action *remove;
        Action *saved64;
        Action *saveg64;
        Action *blank;
        Action *turnon;
        Action *turnoff;
        Action *mode41;
        Action *mode71;
        Action *mode81;
    } myActions;

    static void run(void *a);
    void task();
    void poll();

    void drive_reset(uint8_t doit);
    void set_hw_address(int addr);
    void set_sw_address(int addr);
    void remove_disk(void);
    void insert_disk(bool protect, GcrImage *image);
    void unlink(void);
    void mount_d64(bool protect, File *, int);
    void mount_g64(bool protect, File *);
    void mount_blank(void);
    bool check_if_save_needed(void);
    bool save_if_needed(SubsysCommand *cmd);
    void clear_mfm_dirty_bits();
    bool are_mfm_dirty_bits_set();
    void map_gcr_image_to_mfm(void);
    void swap_disk(SubsysCommand *cmd);
    void wait_for_writeback(void);
    void add_roms_to_cfg_group(void);
    static void mfm_update_callback(void *obj, int pt, int ps, MfmTrack *tr);
    SubsysResultCode_e save_disk_to_file(SubsysCommand *cmd);
    SubsysResultCode_e set_drive_type(t_drive_type drv);
    SubsysResultCode_e change_drive_type(t_drive_type drv,  UserInterface *ui);
    SubsysResultCode_e load_dos_from_file(const char *path, const char *filename);
public:
    C1541(volatile uint8_t *regs, char letter);
    ~C1541();

    static C1541 *get_last_mounted_drive(void);
    
    void init(void);

    // from ObjectWithMenu
    void create_task_items(void);
    void update_task_items(bool);
    // from ConfigurableObject
    void effectuate_settings(void);

    // subsys
    const char *identify(void) { return drive_name.c_str(); }
    SubsysResultCode_e executeCommand(SubsysCommand *cmd);

    // called from IEC (UltiCopy)
    int  get_current_iec_address(void);    
    int  get_effective_iec_address(void);    
    void drive_power(bool on);
    bool get_drive_power();
    t_drive_type get_drive_type() { return current_drive_type; }
    const char *get_drive_type_string();
    const char *get_drive_rom_file(void);
    static void list_roms(ConfigItem *it, IndexedList<char *>& strings);
    void set_rom_config(int idx, const char *fname);

    void get_last_mounted_file(mstring& path, mstring& name);
};

extern C1541 *c1541_A;
extern C1541 *c1541_B;

#endif

