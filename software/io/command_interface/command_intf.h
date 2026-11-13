#ifndef COMMAND_INTF_H
#define COMMAND_INTF_H

#include "integer.h"
#include "config.h"
#include "iomap.h"
#include "subsys.h"
#include "command_target.h"

#define CMD_IF_RAM_BASE  (CMD_IF_BASE + 0x800)

#define CMD_IF_RAM(x)          *((volatile uint8_t *)(CMD_IF_RAM_BASE +x))

#ifndef CMD_IF_SLOT_BASE
#define CMD_IF_SLOT_BASE       *((volatile uint8_t *)(CMD_IF_BASE + 0x0))
#define CMD_IF_SLOT_ENABLE     *((volatile uint8_t *)(CMD_IF_BASE + 0x1))
#endif
// The host tests define the registers as a model of the gateware.
#ifndef CMD_IF_HANDSHAKE_OUT
#define CMD_IF_HANDSHAKE_OUT   *((volatile uint8_t *)(CMD_IF_BASE + 0x2))
#define CMD_IF_STATUSBYTE      *((volatile uint8_t *)(CMD_IF_BASE + 0x3))
#define CMD_IF_COMMAND_START   *((volatile uint8_t *)(CMD_IF_BASE + 0x4))
#define CMD_IF_COMMAND_END     *((volatile uint8_t *)(CMD_IF_BASE + 0x5))
#define CMD_IF_RESPONSE_START  *((volatile uint8_t *)(CMD_IF_BASE + 0x6))
#define CMD_IF_RESPONSE_END    *((volatile uint8_t *)(CMD_IF_BASE + 0x7))
#define CMD_IF_STATUS_START    *((volatile uint8_t *)(CMD_IF_BASE + 0x8))
#define CMD_IF_STATUS_END      *((volatile uint8_t *)(CMD_IF_BASE + 0x9))
#define CMD_IF_STATUS_LENGTH   *((volatile uint8_t *)(CMD_IF_BASE + 0xA))
#define CMD_IF_IRQMASK         *((volatile uint8_t *)(CMD_IF_BASE + 0xB))
#define CMD_IF_RESPONSE_LEN_L  *((volatile uint8_t *)(CMD_IF_BASE + 0xC))
#define CMD_IF_RESPONSE_LEN_H  *((volatile uint8_t *)(CMD_IF_BASE + 0xD))
#define CMD_IF_COMMAND_LEN_L   *((volatile uint8_t *)(CMD_IF_BASE + 0xE))
#define CMD_IF_COMMAND_LEN_H   *((volatile uint8_t *)(CMD_IF_BASE + 0xF))
#define CMD_IF_IRQMASK_SET     *((volatile uint8_t *)(CMD_IF_BASE + 0x4))
#define CMD_IF_IRQMASK_CLEAR   *((volatile uint8_t *)(CMD_IF_BASE + 0x5))
#endif

#define CMD_NEW_COMMAND           0x01
#define CMD_DATA_ACCEPTED         0x02
#define CMD_ABORT_DATA            0x04
#define CMD_MORE_OR_ABORT         0x06
#define CMD_STATE_BITS            0x30
#define CMD_STATE_IDLE            0x00
#define CMD_STATE_PROCESSING      0x10
#define CMD_STATE_DATA_LAST       0x20
#define CMD_STATE_DATA_MORE       0x30

#define HANDSHAKE_RESET           0x87
#define HANDSHAKE_ACCEPT_COMMAND  0x01
#define HANDSHAKE_ACCEPT_NEXTDATA 0x02
#define HANDSHAKE_ACCEPT_ABORT    0x04
#define HANDSHAKE_VALIDATE_LAST   0x10
#define HANDSHAKE_VALIDATE_MORE   0x30

#define CMD_HS_DMA_ACTIVE         0x80
#define CMD_HS_TRIGGER_ACTIVE     0x40

#define CMD_TARGET_NONE 0xFF

#define CMD_MAX_COMMAND_LEN 896
#define CMD_MAX_REPLY_LEN   896
#define CMD_MAX_STATUS_LEN  256


class SubSystem;

// Retain admission identity in the queue; flags alone cannot distinguish an
// old command left behind by a C64 reset from a new command after that reset.
struct CommandInterfaceEvent {
    uint8_t flags;
    OperationCancellation cancellation;
};

class CommandInterface : public SubSystem
{
	TaskHandle_t taskHandle;
	TaskHandle_t resetTaskHandle;

	int response_offset;
	uint8_t *command_buffer;
    uint8_t *response_buffer;
    uint8_t *status_buffer;

    Message incoming_command;
    uint8_t target;    
    void copy_result(Message *data, Message *status);
    void reply_to_command(Message *data, Message *status, bool no_reply, const OperationCancellation& cancellation, bool deferred = false);

    // A deferred command (see command_defer); pending_target is CMD_TARGET_NONE when there
    // is none. Only the UCI Server task touches these; done_ticket is written by the
    // target's own task.
    uint32_t ticket;
    bool defer_requested;
    uint8_t pending_target;
    uint32_t pending_ticket;
    bool pending_no_reply;
    OperationCancellation pending_cancellation;
    volatile uint32_t done_ticket;
    int cart_mode;
    
    static void start_task(void *a);
    static void reset_task(void *a);
    void run_task(void);
    void run_reset_task(void);

public:
	QueueHandle_t queue; // needed from IRQ

	CommandInterface();
    ~CommandInterface();
    
    void dump_registers(void);
    // One pass of the UCI Server task, for one item from the queue.
    void handle_event(const CommandInterfaceEvent& event);
    uint32_t defer(void);
    void deferred_done(uint32_t done);
    const char *identify(void) { return "Command Interface"; }
    bool is_dma_active(void);
    void set_kernal_device_id(uint8_t id);
};

extern CommandInterface cmd_if;

// True once the C64 has been reset, until the UCI task has told the targets.
bool c64_reset_is_pending(void);

// For a long command to ask between steps: has the C64 aborted it (the abort
// bit is live in the status byte), or been reset? The UCI task handles both as
// usual once the command returns.
static inline bool uci_should_stop(void)
{
    return ((CMD_IF_STATUSBYTE & CMD_ABORT_DATA) != 0) || c64_reset_is_pending();
}

#endif
