#ifndef IEC_INTERFACE_H
#define IEC_INTERFACE_H

#include <stdint.h>
#include "iomap.h"
#ifndef RUNS_ON_PC
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#else
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
#endif

// For Info functions
#include "json.h"
#include "stream_textlog.h"
#include "command_intf.h"

#define IECDEBUG 2

#if (IECDEBUG>1)
#define DBGIEC(x) printf(x)
#define DBGIECV(x, ...) printf(x, __VA_ARGS__)
#else
#define DBGIEC(x)
#define DBGIECV(x, ...)
#endif

#define HW_IEC_REGS      IEC_BASE
#define HW_IEC_CODE      (IEC_BASE + 0x800)

#define HW_IEC_RAM(x)          *((volatile uint8_t *)(HW_IEC_CODE +x))
#define HW_IEC_RAM_DW           ((volatile uint32_t *)(HW_IEC_CODE))

#define HW_IEC_VERSION         *((volatile uint8_t *)(HW_IEC_REGS + 0x0))
#define HW_IEC_TX_FIFO_STATUS  *((volatile uint8_t *)(HW_IEC_REGS + 0x1)) // 1: full 0: empty
#define HW_IEC_RX_FIFO_STATUS  *((volatile uint8_t *)(HW_IEC_REGS + 0x2)) // 7: ctrlcode 1: full 0: empty
#define HW_IEC_RESET_ENABLE    *((volatile uint8_t *)(HW_IEC_REGS + 0x3)) // 0 = reset, 1 = run (and reset)
#define HW_IEC_RX_DATA         *((volatile uint8_t *)(HW_IEC_REGS + 0x6)) // read+clear
#define HW_IEC_RX_CTRL         *((volatile uint8_t *)(HW_IEC_REGS + 0x7)) // read
#define HW_IEC_RX_DATA_32      *((volatile uint32_t *)(HW_IEC_REGS + 0x8)) // read+clear
#define HW_IEC_TX_DATA         *((volatile uint8_t *)(HW_IEC_REGS + 0x8)) // push data byte
#define HW_IEC_TX_CTRL         *((volatile uint8_t *)(HW_IEC_REGS + 0x9)) // push control byte
#define HW_IEC_TX_LAST         *((volatile uint8_t *)(HW_IEC_REGS + 0xA)) // push eoi byte
#define HW_IEC_IRQ             *((volatile uint8_t *)(HW_IEC_REGS + 0xC)) // write=ack, bit0=irq enable
#define HW_IEC_TX_FIFO_RELEASE *((volatile uint8_t *)(HW_IEC_REGS + 0xD)) // write any value to release fifo.
#define HW_IEC_IRQ_R           *((volatile uint8_t *)(HW_IEC_REGS + 0xC)) // read
#define HW_IEC_IRQ_BIT        0x01

#define HW_IEC_UP_FIFO_COUNT_LO  *((volatile uint8_t *)(HW_IEC_REGS + 0xE))
#define HW_IEC_UP_FIFO_COUNT_HI  *((volatile uint8_t *)(HW_IEC_REGS + 0xF))

#define IEC_CMD_GO_WARP     0x57
#define IEC_CMD_GO_MASTER   0x1D
#define IEC_CMD_ATN_TO_TX   0x1C
#define IEC_CMD_ATN_RELEASE 0x1B
#define IEC_CMD_ATN_TO_RX   0x1A
#define IEC_CMD_TX_TERM     0x10

#define CTRL_ATN_BEGIN     0x41
#define CTRL_ATN_END       0x42
#define CTRL_READY_FOR_TX  0x43
#define CTRL_READY_FOR_RX  0x44
#define CTRL_EOI           0x45
#define CTRL_JIFFYLOAD     0x46
#define CTRL_BYTE_TXED     0x47
#define CTRL_TX_DONE       0x5a
#define CTRL_RX_DONE       0x5b
#define CTRL_DEV1          0x81
#define CTRL_DEV2          0x82
#define CTRL_DEV3          0x83

#define WARP_ACK           0x57
#define WARP_BLOCK_END     0xAD
#define WARP_TIMEOUT       0xAE
#define WARP_START_RX      0xDA
#define WARP_RX_ERROR      0xDE

#define ERROR_BAD_CTRLCODE 0xE5
#define ERROR_NO_DEVICE    0xE6
#define ERROR_RX_TIMEOUT   0xE9


#define IEC_FIFO_EMPTY 0x01
#define IEC_FIFO_FULL  0x02
#define IEC_FIFO_CTRL  0x80

#define LOGGER_BASE            TRACE_BASE
#define LOGGER_ADDRESS         *((volatile uint32_t *)(LOGGER_BASE + 0x0)) // read
#define LOGGER_LENGTH          *((volatile uint8_t  *)(LOGGER_BASE + 0x4)) // read
#define LOGGER_COMMAND         *((volatile uint8_t  *)(LOGGER_BASE + 0x5)) // write

#define LOGGER_CMD_START       0x33
#define LOGGER_CMD_STOP        0x44

#define SLAVE_CMD_ATN 0x100 // May reset the secondary address
#define SLAVE_CMD_EOI 0x101 

enum t_channel_retval {
    IEC_OK=0, IEC_LAST=1, IEC_NO_DATA=-1, IEC_FILE_NOT_FOUND=-2, IEC_NO_FILE=-3,
    IEC_READ_ERROR=-4, IEC_WRITE_ERROR=-5, IEC_BYTE_LOST=-6, IEC_BUFFER_END=-7
};

class IecSlave
{
public:
    IecSlave() {}
    virtual ~IecSlave() {}

    virtual bool is_enabled(void) { return false; }
    virtual uint8_t get_address(void) { return 20; }
    virtual uint8_t get_type(void) { return 0xEE; }
    virtual const char *iec_identify(void) { return "IEC Slave Prototype"; }
    virtual void info(JSON_Object *) { } 
    virtual void info(StreamTextLog&) { }
    virtual void reset(void) { }

    virtual t_channel_retval prefetch_data(uint8_t&) { return IEC_READ_ERROR; }
    virtual t_channel_retval prefetch_more(int, uint8_t*&, int &) { return IEC_READ_ERROR; }
    virtual t_channel_retval push_ctrl(uint16_t) { return IEC_OK; }
    virtual t_channel_retval push_data(uint8_t) { return IEC_WRITE_ERROR; }
    virtual t_channel_retval pop_data(void) { return IEC_OK; }
    virtual t_channel_retval pop_more(int) { return IEC_OK; }
    virtual void talk(void) { }
};

typedef void (*iec_callback_t)(IecSlave *obj, void *data);
typedef struct {
    IecSlave *obj;
    iec_callback_t func;
    void *data;
} iec_closure_t;

#define MAX_SLOTS 4

class IecInterface
{
    int talker_loc[MAX_SLOTS];
    int listener_loc[MAX_SLOTS];
    int available_slots;
    IecSlave *slaves[MAX_SLOTS];
    IecSlave *addressed_slave;

    TaskHandle_t taskHandle;
    QueueHandle_t queueToIec;

    bool atn;
    bool talking;
    bool enable;
    bool jiffy_load;
    int  jiffy_transfer;

    void get_patch_locations(void);
    void program_processor(void);
    void set_slot_devnum(int slot, uint8_t dev);
    void task(void);
    static void start_task(void *a);

    IecInterface();
public:
    ~IecInterface();

    static IecInterface *get_iec_interface(void);
    void run_from_iec(iec_closure_t *c);

    // Slave management
    int register_slave(IecSlave *slave);
    void unregister_slave(int slot);
    void configure(void);
    void reset(void);

    // Slave Info
    static void info(StreamTextLog &b);
    static void info(JSON_List *obj);
    static void info(Message& msg, int& offs);

    // Master Interface
    void master_open_file(int device, int channel, const char *filename, bool write);
    bool master_send_cmd(int device, uint8_t *cmd, int length);
    void master_read_status(int device);
    bool run_drive_code(int device, uint16_t addr, uint8_t *code, int length);
};

extern IecInterface *iec_if;

#endif
