#ifndef C1581_H
#define C1581_H

#include "c1541.h"

#define C1581_IO_LOC_DRIVE   ((volatile uint8_t *)DRIVE_C_BASE)

/*
#define WD_COMMAND       0x0
#define WD_TRACK         0x1
#define WD_SECTOR        0x2
#define WD_DATAREG       0x3
#define WD_STATUS_CLEAR  0x4
#define WD_STATUS_SET    0x5
#define WD_IRQ_ACK       0x6
#define WD_DMA_MODE      0x7
#define WD_DMA_ADDR      0x8
#define WD_DMA_LEN       0xC
*/

#define WD_STATUS_BUSY      0x01
#define WD_STATUS_INDEX     0x02
#define WD_STATUS_DRQ       0x02
#define WD_STATUS_LOST      0x04
#define WD_STATUS_TRACK_00  0x04
#define WD_STATUS_CRC_ERROR 0x08
#define WD_STATUS_RNF		0x10
#define WD_STATUS_SPINUP_OK 0x20
#define WD_STATUS_REC_TYPE  0x20
#define WD_STATUS_WRITEPROT 0x40
#define WD_STATUS_MOTOR_ON  0x80

#define WD_CMD_RESTORE      0
#define WD_CMD_SEEK    	    1
#define WD_CMD_STEP    	    2
#define WD_CMD_STEP_IN 		4
#define WD_CMD_STEP_OUT     6
#define WD_CMD_READ_SECTOR  8
#define WD_CMD_WRITE_SECTOR 10
#define WD_CMD_READ_ADDR    12
#define WD_CMD_ABORT        13
#define WD_CMD_READ_TRACK   14
#define WD_CMD_WRITE_TRACK  15

#define WD_CMD_UPDATE_BIT   0x10
#define WD_CMD_SPINUP_BIT   0x08
#define WD_CMD_VERIFY_BIT   0x04


typedef struct _wd {
	uint8_t command;
	uint8_t track;
	uint8_t sector;
	uint8_t datareg;
	uint8_t status_clear;
	uint8_t status_set;
	uint8_t irq_ack;
	uint8_t dma_mode;
	uint32_t dma_addr; // little endian
	uint16_t dma_len; // little endian
} wd177x_t;

typedef uint8_t t_1581_cmd;

class C1581 : public SubSystem, ConfigurableObject, ObjectWithMenu
{
    volatile uint8_t *memory_map;
    volatile uint8_t *registers;
    volatile wd177x_t *wd177x;

    t_cfg_definition *local_config_definitions;
    mstring drive_name;
    FileManager *fm;
    
    TaskCategory *taskItemCategory;
    int iec_address;
    char drive_letter;
	
	Flash *flash;
    File *mount_file;
    t_disk_state disk_state;
    TaskHandle_t taskHandle;
    QueueHandle_t cmdQueue;

    struct {
        Action *reset;
        Action *remove;
        Action *blank;
        Action *turnon;
        Action *turnoff;
    } myActions;

    uint8_t buffer[6252];
    uint8_t track, sector, side, step_dir;

    static void run(void *a);

    void task();
    void drive_power(bool on);
    void drive_reset(uint8_t doit);
    void set_hw_address(int addr);
    void set_sw_address(int addr);
    void remove_disk(void);
    void mount_d81(bool protect, File *);
    void handle_wd177x_command(t_1581_cmd& cmd);
    void do_step(bool update);
public:
    C1581(volatile uint8_t *regs, char letter);
    ~C1581();

    void init(void);

    // from ObjectWithMenu
    void create_task_items(void);
    void update_task_items(bool, Path *);
    // from ConfigurableObject
    void effectuate_settings(void);

    // subsys
    const char *identify(void) { return drive_name.c_str(); }
    int  executeCommand(SubsysCommand *cmd);

    // Misc
    int  get_current_iec_address(void);
    bool get_drive_power();

    uint8_t IrqHandler(void);
};

extern C1581 *c1581_C;

#endif

