#include "integer.h"
extern "C" {
	#include "dump_hex.h"
    #include "small_printf.h"
}
#include "command_intf.h"
#include "poll.h"
#include "c64.h"
#include "dos.h"

// this target is a dummy.
CommandTarget cmd_if_empty_target;

// this global will cause us to run!
CommandInterface cmd_if;

// these globals will be filled in by the clients
CommandTarget *command_targets[CMD_IF_MAX_TARGET+1];

// cart definition
extern uint8_t _binary_cmd_test_rom_65_start;
cart_def cmd_cart  = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM };

#define MENU_CMD_RUNCMDCART 0xC180

char *en_dis4[] = { "Disabled", "Enabled" };

#define CFG_CMD_ENABLE  0x71
struct t_cfg_definition cmd_config[] = {
    { CFG_CMD_ENABLE,   CFG_TYPE_ENUM,   "Command Interface",            "%s", en_dis4,    0,  1, 0 },
    { CFG_TYPE_END,     CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};


void poll_command_interface(Event &ev)
{
    cmd_if.poll(ev);
}

CommandInterface :: CommandInterface()
{
    for(int i=0;i<=CMD_IF_MAX_TARGET;i++)
        command_targets[i] = &cmd_if_empty_target;
    
    if(getFpgaCapabilities() & CAPAB_COMMAND_INTF) {
        register_store(0x434D4E44, "Ultimate Command Interface", cmd_config);
        CMD_IF_SLOT_BASE = 0x47; // $DF1C
        CMD_IF_SLOT_ENABLE = 0; // DISABLE until we know we can enable ourselves: cfg->get_value(CFG_CMD_ENABLE);
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;    
        poll_list.append(&poll_command_interface);
    	main_menu_objects.append(this);
    
        // dump_registers();
    
        response_buffer = (uint8_t *)(CMD_IF_RAM_BASE + (8*CMD_IF_RESPONSE_START));
        status_buffer   = (uint8_t *)(CMD_IF_RAM_BASE + (8*CMD_IF_STATUS_START));
        command_buffer  = (uint8_t *)(CMD_IF_RAM_BASE + (8*CMD_IF_COMMAND_START));
    
        incoming_command.message = command_buffer;
        incoming_command.length = 0;
        
    }
    target = CMD_TARGET_NONE;
    cart_mode = 0;
}

CommandInterface :: ~CommandInterface()
{
	poll_list.remove(&poll_command_interface);

    CMD_IF_SLOT_ENABLE = 0;
}

void CommandInterface :: effectuate_settings(void)
{
    printf("CMD_IF: Effectuate\n");
    if(cart_mode & CART_REU) {
        CMD_IF_SLOT_ENABLE = cfg->get_value(CFG_CMD_ENABLE);
    } else {
        CMD_IF_SLOT_ENABLE = 0;
    }                    
}
    
int CommandInterface :: poll(Event &e)
{
    int length;

    if(e.type == e_cart_mode_change) {
        printf("CommandInterface received a cart mode change to %b.\n", e.param);
        cart_mode = e.param;
        effectuate_settings();
	} else if((e.type == e_object_private_cmd)&&(e.object == this)) {
        switch(e.param) {
        case MENU_CMD_RUNCMDCART:
            cmd_cart.custom_addr = (void *)&_binary_cmd_test_rom_65_start;
            push_event(e_unfreeze, (void *)&cmd_cart, 1);
            CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;    
            //sprintf((char *)response_buffer, "ULTIMATE-II V2.6");
            //CMD_IF_RESPONSE_LEN_H = 0;
            //CMD_IF_RESPONSE_LEN_L = 16;
            break;
        default:
            break;
        }      
    }
  
    Message *data, *status;
    
    uint8_t status_byte = CMD_IF_STATUSBYTE;

    if(status_byte & CMD_ABORT_DATA) {
        printf("Abort received.\n");
        if (target != CMD_TARGET_NONE) {
            command_targets[target]->abort();
        }
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;
    }
    if(status_byte & CMD_DATA_ACCEPTED) {
        if (target != CMD_TARGET_NONE) {
            command_targets[target]->get_more_data(&data, &status);
            copy_result(data, status);
        }
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_NEXTDATA;
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
    }
    return 0;
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

int  CommandInterface :: fetch_task_items(IndexedList<Action*> &item_list)
{
    //item_list.append(new ObjectMenuItem(this, "Run Command Cart", MENU_CMD_RUNCMDCART));  /* temporary item */
    //return 1;
    return 0;
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
