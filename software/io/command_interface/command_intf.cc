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

#define CMD_IF_DEBUG 0


extern "C" {
void ResetInterruptHandlerCmdIf()
{
    BaseType_t woken;
    uint8_t new_flags = CMD_ABORT_DATA;
    xSemaphoreGiveFromISR(resetSemaphore, &woken);
    xQueueSendFromISR(cmd_if.queue, &new_flags, &woken);
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
        
        queue = xQueueCreate(16, sizeof(uint8_t));
        xTaskCreate( CommandInterface :: start_task, "UCI Server", configMINIMAL_STACK_SIZE, this, PRIO_HW_SERVICE, &taskHandle );
        xTaskCreate( CommandInterface :: reset_task, "UCI Reset Server", configMINIMAL_STACK_SIZE, this, PRIO_HW_SERVICE, &resetTaskHandle );

        resetSemaphore = xSemaphoreCreateBinary();
    }
    target = CMD_TARGET_NONE;
    cart_mode = 0;
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

	BaseType_t retval;
    if (xQueueSendFromISR(cmd_if.queue, &new_flags, &retval) != pdTRUE) {
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
	int length;
    Message *data, *status;
    uint8_t status_byte;

    CMD_IF_IRQMASK_CLEAR = 7;
    ioWrite8(ITU_IRQ_ENABLE, ITU_INTERRUPT_CMDIF);

    while(1) {
		xQueueReceive(queue, &status_byte, portMAX_DELAY);
#if CMD_IF_DEBUG
		printf("{%b}", status_byte);
#endif
		if(status_byte & CMD_ABORT_DATA) {
			//printf("Abort received.\n");
			if (target != CMD_TARGET_NONE) {
			    int offset = (((int)CMD_IF_RESPONSE_LEN_H) << 8) | CMD_IF_RESPONSE_LEN_L;
				command_targets[target]->abort(offset - response_offset);
			}
			CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;
			CMD_IF_IRQMASK_CLEAR = CMD_ABORT_DATA;
		}
		if(status_byte & CMD_DATA_ACCEPTED) {
			if (target != CMD_TARGET_NONE) {
				command_targets[target]->get_more_data(&data, &status);
				copy_result(data, status);
			} else {
				printf("CMDIF: Requested for more data, but target is not set!\n");
			}
            CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_NEXTDATA;
            CMD_IF_IRQMASK_CLEAR = CMD_DATA_ACCEPTED;
		}

        if (status_byte & CMD_NEW_COMMAND) {
            length = int(CMD_IF_COMMAND_LEN_L) + (int(CMD_IF_COMMAND_LEN_H) << 8);

            if (length) {
#if CMD_IF_DEBUG
                printf("Command received (%b):\n", CMD_IF_STATUSBYTE);
                dump_hex_relative((void *)command_buffer, length);
#endif
                incoming_command.length = length;
                target = incoming_command.message[0] & CMD_IF_MAX_TARGET;
                bool no_reply = ((incoming_command.message[0] & CMD_IF_NO_REPLY) != 0);
                command_targets[target]->parse_command(&incoming_command, &data, &status);

                CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_COMMAND; // clears CMD_NEW_COMMAND
                if (no_reply) {
                    CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET; // forces state to 00
                } else {
                    copy_result(data, status); // sets state to 10 or 11
                }
            } else {
                printf("Null command.\n");
                CMD_IF_RESPONSE_LEN_H = 0;
                CMD_IF_RESPONSE_LEN_L = 0;
                CMD_IF_STATUS_LENGTH = 0;
                CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_COMMAND;
                CMD_IF_HANDSHAKE_OUT = HANDSHAKE_VALIDATE_LAST;
            }
            CMD_IF_IRQMASK_CLEAR = CMD_NEW_COMMAND;
        }
    }
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
    printf("CMD_IF_REPSONSE_LEN_H  %b\n", CMD_IF_RESPONSE_LEN_H);
    printf("CMD_IF_COMMAND_LEN_L   %b\n", CMD_IF_COMMAND_LEN_L );
    printf("CMD_IF_COMMAND_LEN_H   %b\n", CMD_IF_COMMAND_LEN_H );
    printf("CMD_IF_IRQMASK         %b\n", CMD_IF_IRQMASK       );
}

Message c_message_no_target      = {  9, true, (uint8_t *)"NO TARGET" }; 
Message c_status_ok              = {  5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty          = {  0, true, (uint8_t *)"" };
