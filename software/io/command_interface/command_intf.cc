#include "integer.h"
extern "C" {
	#include "dump_hex.h"
    #include "small_printf.h"
}
#include "command_intf.h"
#include "c64.h"
#include "sampler.h"

// this target is a dummy.
CommandTarget cmd_if_empty_target;

// this global will cause us to run!
CommandInterface cmd_if;

// these globals will be filled in by the clients
CommandTarget *command_targets[CMD_IF_MAX_TARGET+1];

// Semaphore set by interrupt
static SemaphoreHandle_t resetSemaphore;
// Set by the reset ISR for the server task, so a target is told about the
// reset between commands rather than during one.
static volatile bool c64_reset_pending;
static volatile bool command_abort_pending;
static CancellationSource command_cancellation;

bool c64_reset_is_pending(void)
{
    return c64_reset_pending;
}

#define CMD_IF_DEBUG 0

// The UCI Server task runs at PRIO_C64_SERVICE, above the network services, so that
// abort, reset, the handshake and a deferred reply never wait for FTP, REST or telnet.
// A target's own work can be long (storage, or the network for a target that does not
// defer); it runs at PRIO_HW_SERVICE, the level all of it ran at before, so it does not
// starve the services.
class TargetWork
{
public:
    TargetWork() { vTaskPrioritySet(NULL, PRIO_HW_SERVICE); }
    ~TargetWork() { vTaskPrioritySet(NULL, PRIO_C64_SERVICE); }
};


extern "C" {
void ResetInterruptHandlerCmdIf()
{
    BaseType_t woken = pdFALSE;
    uint8_t new_flags = CMD_ABORT_DATA;
    c64_reset_pending = true;
    command_abort_pending = true;
    command_cancellation.signal();
    xSemaphoreGiveFromISR(resetSemaphore, &woken);
    CommandInterfaceEvent event = { new_flags, command_cancellation.token() };
    xQueueSendFromISR(cmd_if.queue, &event, &woken);
}
}

CommandInterface :: CommandInterface() : SubSystem(SUBSYSID_CMD_IF)
{
    for(int i=0;i<=CMD_IF_MAX_TARGET;i++)
        command_targets[i] = &cmd_if_empty_target;

    if((getFpgaCapabilities() & CAPAB_COMMAND_INTF) && (getFpgaCapabilities() & CAPAB_CARTRIDGE)) {
        CMD_IF_SLOT_BASE = 0x47; // $DF1C
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;    
    
        // dump_registers();
    
        response_offset = (8*CMD_IF_RESPONSE_START);
        response_buffer = (uint8_t *)(CMD_IF_RAM_BASE + response_offset);
        status_buffer   = (uint8_t *)(CMD_IF_RAM_BASE + (8*CMD_IF_STATUS_START));
        command_buffer  = (uint8_t *)(CMD_IF_RAM_BASE + (8*CMD_IF_COMMAND_START));
    
        incoming_command.message = command_buffer;
        incoming_command.length = 0;
        
        queue = xQueueCreate(16, sizeof(CommandInterfaceEvent));
        xTaskCreate( CommandInterface :: start_task, "UCI Server", configMINIMAL_STACK_SIZE, this, PRIO_C64_SERVICE, &taskHandle );
        xTaskCreate( CommandInterface :: reset_task, "UCI Reset Server", configMINIMAL_STACK_SIZE, this, PRIO_HW_SERVICE, &resetTaskHandle );

        resetSemaphore = xSemaphoreCreateBinary();
    }
    target = CMD_TARGET_NONE;
    cart_mode = 0;
    ticket = 0;
    defer_requested = false;
    pending_target = CMD_TARGET_NONE;
    pending_ticket = 0;
    pending_no_reply = false;
    done_ticket = 0;
}

CommandInterface :: ~CommandInterface()
{
    ioWrite8(ITU_IRQ_DISABLE, ITU_INTERRUPT_CMDIF);
}
    
void CommandInterface :: start_task(void *a)
{
	CommandInterface *uci = (CommandInterface *)a;
	uci->run_task();
}

void CommandInterface :: reset_task(void *a)
{
    CommandInterface *uci = (CommandInterface *)a;
    uci->run_reset_task();
}

extern "C" BaseType_t command_interface_irq(void)
{
	uint8_t status_byte = CMD_IF_STATUSBYTE;
	uint8_t new_flags = status_byte & ~CMD_IF_IRQMASK;
	CMD_IF_IRQMASK_SET = new_flags;

    if (new_flags & CMD_ABORT_DATA) {
        command_abort_pending = true;
        command_cancellation.signal();
    }

	BaseType_t retval = pdFALSE;
    CommandInterfaceEvent event = { new_flags, command_cancellation.token() };
    if (xQueueSendFromISR(cmd_if.queue, &event, &retval) != pdTRUE) {
        outbyte('\\');
    }
	return retval;
}

void CommandInterface :: run_reset_task(void)
{
    // enable IRQ on C64 reset
    ioWrite8(ITU_IRQ_CLEAR,  ITU_INTERRUPT_RESET);
    ioWrite8(ITU_IRQ_ENABLE, ITU_INTERRUPT_RESET);

    while (1) {
        xSemaphoreTake(resetSemaphore, portMAX_DELAY);
        // now also clear the audio sampler stuff
        Sampler :: reset();
    }
}

void CommandInterface :: run_task(void)
{
    CommandInterfaceEvent event;

    CMD_IF_IRQMASK_CLEAR = 7;
    ioWrite8(ITU_IRQ_ENABLE, ITU_INTERRUPT_CMDIF);

    while(1) {
		xQueueReceive(queue, &event, portMAX_DELAY);
		handle_event(event);
	}
}

void CommandInterface :: handle_event(const CommandInterfaceEvent& event)
{
    uint8_t status_byte = event.flags;
    const OperationCancellation& cancellation = event.cancellation;
	int length;
    Message *data, *status;
#if CMD_IF_DEBUG
	printf("{%b}", status_byte);
#endif
	// Checked on every event, so a reset whose queue post was dropped is
	// still acted on. Read and cleared as one step: a reset landing
	// between the two would be cleared without ever reaching the targets.
	portENTER_CRITICAL();
	bool reset_seen = c64_reset_pending;
	c64_reset_pending = false;
    bool abort_seen = command_abort_pending;
    command_abort_pending = false;
	portEXIT_CRITICAL();
	// Ends a deferred command even when the abort item of the reset was dropped.
	if (reset_seen || abort_seen) {
		status_byte |= CMD_ABORT_DATA;
	}
	bool aborted = false;
	int abort_offset = 0;
	if(status_byte & CMD_ABORT_DATA) {
		//printf("Abort received.\n");
		uint8_t unmask = CMD_ABORT_DATA;
		// A deferred command ends here; its late result fails the ticket check. The
		// reset below clears its new-command flag, so its interrupt can be unmasked.
		if (pending_target != CMD_TARGET_NONE) {
			unmask |= CMD_NEW_COMMAND;
			pending_target = CMD_TARGET_NONE;
		}
		// Read before the reset below rewinds the response pointer.
		int offset = (((int)CMD_IF_RESPONSE_LEN_H) << 8) | CMD_IF_RESPONSE_LEN_L;
		abort_offset = offset - response_offset;
		aborted = true;
		CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;
		CMD_IF_IRQMASK_CLEAR = unmask | CMD_NEW_COMMAND | CMD_DATA_ACCEPTED;
	}
	// The C64 is free now. The targets clean up after it at their own level, before this
	// task looks at anything else: freeing a large body or popping a file is their work.
	if (reset_seen || aborted) {
		TargetWork work;
		if (reset_seen) {
			for(int i=0;i<=CMD_IF_MAX_TARGET;i++)
				command_targets[i]->c64_reset();
		}
		if (aborted && (target != CMD_TARGET_NONE)) {
			command_targets[target]->abort(abort_offset);
		}
	}
    if (reset_seen || aborted || cancellation.cancelled()) return;
	if(status_byte & CMD_DATA_ACCEPTED) {
		if (target != CMD_TARGET_NONE) {
			{
				TargetWork work;
				command_targets[target]->get_more_data(&data, &status);
			}
            bool published = false;
            portENTER_CRITICAL();
            if (!cancellation.cancelled()) {
                command_targets[target]->prepare_reply(&data, &status);
                copy_result(data, status);
                CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_NEXTDATA;
                published = true;
            }
            portEXIT_CRITICAL();
            command_targets[target]->finish_reply(published);
        } else {
            portENTER_CRITICAL();
            if (!cancellation.cancelled()) CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_NEXTDATA;
            portEXIT_CRITICAL();
        }
        CMD_IF_IRQMASK_CLEAR = CMD_DATA_ACCEPTED;
	}

    if (status_byte & CMD_NEW_COMMAND) {
        length = int(CMD_IF_COMMAND_LEN_L) + (int(CMD_IF_COMMAND_LEN_H) << 8);
        pending_target = CMD_TARGET_NONE;
        defer_requested = false;

        if (length) {
#if CMD_IF_DEBUG
            printf("Command received (%b):\n", CMD_IF_STATUSBYTE);
            dump_hex_relative((void *)command_buffer, length);
#endif
            incoming_command.length = length;
            target = incoming_command.message[0] & CMD_IF_MAX_TARGET;
            bool no_reply = ((incoming_command.message[0] & CMD_IF_NO_REPLY) != 0);
            ticket++;
            command_targets[target]->begin_command(cancellation);
            if (command_targets[target]->is_quick(&incoming_command)) {
                command_targets[target]->parse_command(&incoming_command, &data, &status);
            } else {
                TargetWork work;
                command_targets[target]->parse_command(&incoming_command, &data, &status);
            }

            if (defer_requested) {
                // Nothing changes for the C64: the command stays BUSY, not accepted, until
                // the reply is sent below on a later event. (The gateware does not guard the
                // command buffer in any state; a target that defers keeps its own copy.)
                // The interrupt for a new command stays masked until then.
                pending_target = target;
                pending_ticket = ticket;
                pending_cancellation = cancellation;
                pending_no_reply = no_reply;
            } else {
                reply_to_command(data, status, no_reply, cancellation);
            }
        } else {
            printf("Null command.\n");
            portENTER_CRITICAL();
            if (!cancellation.cancelled()) {
                CMD_IF_RESPONSE_LEN_H = 0;
                CMD_IF_RESPONSE_LEN_L = 0;
                CMD_IF_STATUS_LENGTH = 0;
                CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_COMMAND;
                CMD_IF_HANDSHAKE_OUT = HANDSHAKE_VALIDATE_LAST;
            }
            portEXIT_CRITICAL();
        }
        if (!defer_requested) {
            CMD_IF_IRQMASK_CLEAR = CMD_NEW_COMMAND;
        }
    }

    // Checked on every event, so a completion whose wake-up was dropped from a full
    // queue is still sent. The gateware validates in any state, so a result is sent
    // only while its own command is still BUSY: an abort, or a reset handled above,
    // has already ended it, and a late result would put a reply in front of the next
    // command.
    if ((pending_target != CMD_TARGET_NONE) && (done_ticket == pending_ticket)) {
        uint8_t now = CMD_IF_STATUSBYTE;
        if (((now & CMD_STATE_BITS) == CMD_STATE_PROCESSING) && !(now & CMD_ABORT_DATA)) {
            uint8_t done_target = pending_target;
            pending_target = CMD_TARGET_NONE;
            {
                TargetWork work;
                command_targets[done_target]->deferred_reply(&data, &status);
            }
            reply_to_command(data, status, pending_no_reply, pending_cancellation);
            CMD_IF_IRQMASK_CLEAR = CMD_NEW_COMMAND;
        }
    }
}

void CommandInterface :: reply_to_command(Message *data, Message *status, bool no_reply,
                                          const OperationCancellation& cancellation)
{
    bool published = false;
    portENTER_CRITICAL();
    if (!cancellation.cancelled()) {
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_COMMAND;
        if (no_reply) {
            CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;
        } else {
            command_targets[target]->prepare_reply(&data, &status);
            copy_result(data, status);
            published = true;
        }
    }
    portEXIT_CRITICAL();
    command_targets[target]->finish_reply(published);
}

uint32_t CommandInterface :: defer(void)
{
    defer_requested = true;
    return ticket;
}

// Called by the task that did the work. done_ticket is written BEFORE the wake-up: this
// task, being higher, runs as soon as the wake-up is posted, and must find the ticket. A
// full queue means that it is awake already, and it checks done_ticket on every event.
// Tickets only grow, and a late report of an older one must not hide a newer one (a
// second deferring target, or a worker that was preempted before it reported).
void CommandInterface :: deferred_done(uint32_t done)
{
    CommandInterfaceEvent wake = { 0, command_cancellation.token() };
    portENTER_CRITICAL();
    if ((int32_t)(done - done_ticket) > 0) {
        done_ticket = done;
    }
    portEXIT_CRITICAL();
    xQueueSend(queue, &wake, 0);
}

uint32_t command_defer(void)
{
    return cmd_if.defer();
}

void command_deferred_done(uint32_t ticket)
{
    cmd_if.deferred_done(ticket);
}

void CommandInterface :: copy_result(Message *data, Message *status)
{
#if CMD_IF_DEBUG
    printf("data:\n");
    dump_hex_relative((void *)data->message, data->length);
    printf("status:\n");
    dump_hex_relative((void *)status->message, status->length);
#endif
    memcpy(response_buffer, data->message, data->length);
    memcpy(status_buffer, status->message, status->length);
    CMD_IF_RESPONSE_LEN_H = uint8_t(data->length >> 8);
    CMD_IF_RESPONSE_LEN_L = uint8_t(data->length);
    CMD_IF_STATUS_LENGTH = status->length;
    if(data->last_part) {
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_VALIDATE_LAST;
    } else {
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_VALIDATE_MORE;
    }    
}

bool CommandInterface :: is_dma_active(void)
{
    return ((CMD_IF_HANDSHAKE_OUT & CMD_HS_DMA_ACTIVE) != 0);
}

void CommandInterface :: set_kernal_device_id(uint8_t id)
{
    CMD_IF_SLOT_ENABLE = 0x80 | id;
}

void CommandInterface :: dump_registers(void)
{
    printf("CMD_IF_SLOT_BASE       %b\n", CMD_IF_SLOT_BASE     );
    printf("CMD_IF_SLOT_ENABLE     %b\n", CMD_IF_SLOT_ENABLE   );
    printf("CMD_IF_HANDSHAKE_OUT   %b\n", CMD_IF_HANDSHAKE_OUT );
    printf("CMD_IF_STATUSBYTE      %b\n", CMD_IF_STATUSBYTE    );
    printf("CMD_IF_COMMAND_START   %b\n", CMD_IF_COMMAND_START );
    printf("CMD_IF_COMMAND_END     %b\n", CMD_IF_COMMAND_END   );
    printf("CMD_IF_RESPONSE_START  %b\n", CMD_IF_RESPONSE_START);
    printf("CMD_IF_RESPONSE_END    %b\n", CMD_IF_RESPONSE_END  );
    printf("CMD_IF_STATUS_START    %b\n", CMD_IF_STATUS_START  );
    printf("CMD_IF_STATUS_END      %b\n", CMD_IF_STATUS_END    );
    printf("CMD_IF_STATUS_LENGTH   %b\n", CMD_IF_STATUS_LENGTH );
    printf("CMD_IF_RESPONSE_LEN_L  %b\n", CMD_IF_RESPONSE_LEN_L);
    printf("CMD_IF_RESPONSE_LEN_H  %b\n", CMD_IF_RESPONSE_LEN_H);
    printf("CMD_IF_COMMAND_LEN_L   %b\n", CMD_IF_COMMAND_LEN_L );
    printf("CMD_IF_COMMAND_LEN_H   %b\n", CMD_IF_COMMAND_LEN_H );
    printf("CMD_IF_IRQMASK         %b\n", CMD_IF_IRQMASK       );
}

Message c_message_no_target      = {  9, true, (uint8_t *)"NO TARGET" }; 
Message c_status_ok              = {  5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty          = {  0, true, (uint8_t *)"" };
