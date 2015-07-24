#include "integer.h"
extern "C" {
	#include "dump_hex.h"
    #include "small_printf.h"
}
#include "command_intf.h"
#include "c64.h"
#include "dos.h"

// this target is a dummy.
CommandTarget cmd_if_empty_target;

// this global will cause us to run!
CommandInterface cmd_if;

// these globals will be filled in by the clients
CommandTarget *command_targets[CMD_IF_MAX_TARGET+1];

// cart definition
extern uint8_t _cmd_test_rom_65_start;
cart_def cmd_cart  = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM };

#define MENU_CMD_RUNCMDCART 0xC180

CommandInterface :: CommandInterface() : SubSystem(SUBSYSID_CMD_IF)
{
    for(int i=0;i<=CMD_IF_MAX_TARGET;i++)
        command_targets[i] = &cmd_if_empty_target;
    
    cmd_cart.custom_addr = (void *)&_cmd_test_rom_65_start;

    if(getFpgaCapabilities() & CAPAB_COMMAND_INTF) {
        CMD_IF_SLOT_BASE = 0x47; // $DF1C
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;    
    
        // dump_registers();
    
        response_buffer = (uint8_t *)(CMD_IF_RAM_BASE + (8*CMD_IF_RESPONSE_START));
        status_buffer   = (uint8_t *)(CMD_IF_RAM_BASE + (8*CMD_IF_STATUS_START));
        command_buffer  = (uint8_t *)(CMD_IF_RAM_BASE + (8*CMD_IF_COMMAND_START));
    
        incoming_command.message = command_buffer;
        incoming_command.length = 0;
        
    }
    target = CMD_TARGET_NONE;
    cart_mode = 0;

    queue = xQueueCreate(16, sizeof(uint8_t));
    xTaskCreate( CommandInterface :: start_task, "UCI Server", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 3, &taskHandle );
}

CommandInterface :: ~CommandInterface()
{
}
    
int CommandInterface :: executeCommand(SubsysCommand *cmd)
{
    CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;
	SubsysCommand *c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_START_CART, (int)&cmd_cart, "", "");
	return c64_command->execute();
}

void CommandInterface :: start_task(void *a)
{
	CommandInterface *uci = (CommandInterface *)a;
	uci->run_task();
}

extern "C" BaseType_t command_interface_irq(void) {

	uint8_t status_byte = CMD_IF_STATUSBYTE;
	uint8_t new_flags = status_byte & ~CMD_IF_IRQMASK;
	CMD_IF_IRQMASK_SET = status_byte;

	BaseType_t retval;
	xQueueSendFromISR(cmd_if.queue, &new_flags, &retval);
	return retval;
}

void CommandInterface :: run_task(void)
{
	int length;
    Message *data, *status;
    uint8_t status_byte;

    while(1) {
		xQueueReceive(queue, &status_byte, portMAX_DELAY);

		if(status_byte & CMD_ABORT_DATA) {
			printf("Abort received.\n");
			if (target != CMD_TARGET_NONE) {
				command_targets[target]->abort();
			}
			CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;
			CMD_IF_IRQMASK_CLEAR = CMD_ABORT_DATA;
		}
		if(status_byte & CMD_DATA_ACCEPTED) {
			if (target != CMD_TARGET_NONE) {
				command_targets[target]->get_more_data(&data, &status);
				copy_result(data, status);
			}
			CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_NEXTDATA;
			CMD_IF_IRQMASK_CLEAR = CMD_DATA_ACCEPTED;
		}

		if(status_byte & CMD_NEW_COMMAND) {
			length = int(CMD_IF_COMMAND_LEN_L) + (int(CMD_IF_COMMAND_LEN_H) << 8);

			if (length) {
				//printf("Command received:\n");
				//dump_hex_relative((void *)command_buffer, length);

				incoming_command.length = length;
				target = incoming_command.message[0] & CMD_IF_MAX_TARGET;
				command_targets[target]->parse_command(&incoming_command, &data, &status);
				CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_COMMAND;
				copy_result(data, status);

			} else {
				printf("Null command.\n");
				CMD_IF_RESPONSE_LEN_H = 0;
				CMD_IF_RESPONSE_LEN_L = 0;
				CMD_IF_STATUS_LENGTH = 0;
				CMD_IF_HANDSHAKE_OUT = HANDSHAKE_VALIDATE_LAST;
			}
			CMD_IF_IRQMASK_CLEAR = CMD_NEW_COMMAND;
		}
    }
}

void CommandInterface :: copy_result(Message *data, Message *status)
{
    //printf("data:\n");
    //dump_hex_relative((void *)data->message, data->length);
    //printf("status:\n");
    //dump_hex_relative((void *)status->message, status->length);
    memcpy(response_buffer, data->message, data->length);
    memcpy(status_buffer, status->message, status->length);
    CMD_IF_RESPONSE_LEN_H = uint8_t(data->length >> 8);
    CMD_IF_RESPONSE_LEN_L = uint8_t(data->length);
    CMD_IF_STATUS_LENGTH = status->length;
    if(data->last_part) {
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_VALIDATE_LAST;
        target = CMD_TARGET_NONE;
    } else {
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_VALIDATE_MORE;
    }    
}

int  CommandInterface :: fetch_task_items(Path *path, IndexedList<Action*> &item_list)
{
    item_list.append(new Action("Run Command Cart", getID(), MENU_CMD_RUNCMDCART, 0));
    return 1;
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
}

Message c_message_no_target      = {  9, true, (uint8_t *)"NO TARGET" }; 
Message c_status_ok              = {  5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty          = {  0, true, (uint8_t *)"" };
