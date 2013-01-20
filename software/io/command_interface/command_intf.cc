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
extern BYTE _binary_cmd_test_rom_65_start;
cart_def cmd_cart  = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM };

#define MENU_CMD_RUNCMDCART 0xC180

void poll_command_interface(Event &ev)
{
    cmd_if.poll(ev);
}

CommandInterface :: CommandInterface()
{
    for(int i=0;i<=CMD_IF_MAX_TARGET;i++)
        command_targets[i] = &cmd_if_empty_target;
    
    if(CAPABILITIES & CAPAB_COMMAND_INTF) {
        CMD_IF_SLOT_BASE = 0x47; // $DF1C
        CMD_IF_SLOT_ENABLE = 1;
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;    
        poll_list.append(&poll_command_interface);
    	main_menu_objects.append(this);
    
        dump_registers();
    
        response_buffer = (BYTE *)(CMD_IF_RAM_BASE + (8*CMD_IF_RESPONSE_START));
        status_buffer   = (BYTE *)(CMD_IF_RAM_BASE + (8*CMD_IF_STATUS_START));
        command_buffer  = (BYTE *)(CMD_IF_RAM_BASE + (8*CMD_IF_COMMAND_START));
    
        incoming_command.message = command_buffer;
        incoming_command.length = 0;
        
    }
    target = CMD_TARGET_NONE;
}

CommandInterface :: ~CommandInterface()
{
	poll_list.remove(&poll_command_interface);

    CMD_IF_SLOT_ENABLE = 0;
}

/*
int CommandInterface :: poll(Event &e)
{
//    printf("Poll Command IF!\n");
    int length;
    BYTE status_byte = CMD_IF_STATUSBYTE;

    if(e.type == e_cart_mode_change) {
        printf("CommandInterface received a cart mode change to %b.\n", e.param);
        if(e.param & CART_REU) {
            CMD_IF_SLOT_ENABLE = 1;
        } else {
            CMD_IF_SLOT_ENABLE = 0;
        }                    
	} else if((e.type == e_object_private_cmd)&&(e.object == this)) {
        case MENU_CMD_RUNCMDCART:
            cmd_cart.custom_addr = (void *)&_binary_cmd_test_rom_65_start;
            push_event(e_unfreeze, (void *)&cmd_cart, 1);
            break;
    }
    
    if(status_byte & CMD_ABORT_DATA) {
//        printf("Abort bit cleared, we were not transmitting data.\n");
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_ABORT;
    }
    if(status_byte & CMD_DATA_ACCEPTED) {
//        printf("You tell me you accepted data, but I didn't give you any.\n");
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_NEXTDATA;
    }
    
    // This demo implements a blocking loop only
    if(status_byte & CMD_NEW_COMMAND) {
        length = int(CMD_IF_COMMAND_LEN_L) + (int(CMD_IF_COMMAND_LEN_H) << 8);

        if (length) {
            printf("Command received:\n");
            dump_hex_relative((void *)command_buffer, length);
    
            if(strncmp((char *)command_buffer, "GIDEON", 6)==0) {
                sprintf((char *)response_buffer, "YES, EXACTLY! THAT'S ME! KEEP TRYING!");
                CMD_IF_RESPONSE_LEN_H = 0;
                CMD_IF_RESPONSE_LEN_L = 37;
                CMD_IF_HANDSHAKE_OUT  = HANDSHAKE_VALIDATE_LAST; // easy
                
            } else if (strncmp((char *)command_buffer, "COUNTER", 7)==0) {
                for(int i=0;i<15;i++) {
                    wait_ms(500);
                    sprintf((char *)response_buffer, "%3d", i);
                    CMD_IF_RESPONSE_LEN_H = 0;
                    CMD_IF_RESPONSE_LEN_L = 3;
                    CMD_IF_HANDSHAKE_OUT  = HANDSHAKE_VALIDATE_MORE;
                    while(!(CMD_IF_STATUSBYTE & CMD_MORE_OR_ABORT))
                        ;
                    if(CMD_IF_STATUSBYTE & CMD_ABORT_DATA) {
                        break;
                    }
                    CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_NEXTDATA;
                }
                if(CMD_IF_STATUSBYTE & CMD_ABORT_DATA) {
                    CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_ABORT | HANDSHAKE_ACCEPT_NEXTDATA;
                    sprintf((char *)status_buffer, "ABORTED");
                    CMD_IF_STATUS_LENGTH = 7;
                    CMD_IF_HANDSHAKE_OUT  = HANDSHAKE_VALIDATE_LAST; // makes the statemachine go back to idle.
                } else {
                    sprintf((char *)response_buffer, "DONE");
                    CMD_IF_RESPONSE_LEN_H = 0;
                    CMD_IF_RESPONSE_LEN_L = 4;
                    CMD_IF_HANDSHAKE_OUT  = HANDSHAKE_VALIDATE_LAST;
                }
            } else {
                sprintf((char *)response_buffer, "UNKNOWN COMMAND");
                CMD_IF_RESPONSE_LEN_H = 0;
                CMD_IF_RESPONSE_LEN_L = 16;
                sprintf((char *)status_buffer, "STATUS");
                CMD_IF_STATUS_LENGTH = 6;
                CMD_IF_HANDSHAKE_OUT  = HANDSHAKE_VALIDATE_LAST;
            }

        } else {
            printf("Null command.\n");
            CMD_IF_RESPONSE_LEN_H = 0;
            CMD_IF_RESPONSE_LEN_L = 0;
            CMD_IF_STATUS_LENGTH = 0;
        }
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_COMMAND; // resets the command valid bit and the pointers.
    }
    return 0;
}
*/

int CommandInterface :: poll(Event &e)
{
    int length;

    if(e.type == e_cart_mode_change) {
        printf("CommandInterface received a cart mode change to %b.\n", e.param);
        if(e.param & CART_REU) {
            CMD_IF_SLOT_ENABLE = 1;
        } else {
            CMD_IF_SLOT_ENABLE = 0;
        }                    
	} else if((e.type == e_object_private_cmd)&&(e.object == this)) {
        switch(e.param) {
        case MENU_CMD_RUNCMDCART:
            cmd_cart.custom_addr = (void *)&_binary_cmd_test_rom_65_start;
            push_event(e_unfreeze, (void *)&cmd_cart, 1);
            CMD_IF_HANDSHAKE_OUT = HANDSHAKE_RESET;    
            sprintf((char *)response_buffer, "ULTIMATE-II V2.6");
            CMD_IF_RESPONSE_LEN_H = 0;
            CMD_IF_RESPONSE_LEN_L = 16;
            break;
        default:
            break;
        }      
    }
  
    Message *data, *status;
    
    BYTE status_byte = CMD_IF_STATUSBYTE;

    if(status_byte & CMD_ABORT_DATA) {
        printf("Abort received.\n");
        if (target != CMD_TARGET_NONE) {
            command_targets[target]->abort();
        }
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_ACCEPT_ABORT;
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
            printf("Command received:\n");
            dump_hex_relative((void *)command_buffer, length);
    
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
    printf("data:\n");
    dump_hex_relative((void *)data->message, data->length);
    printf("status:\n");
    dump_hex_relative((void *)status->message, status->length);
    memcpy(response_buffer, data->message, data->length);
    memcpy(status_buffer, status->message, status->length);
    CMD_IF_RESPONSE_LEN_H = BYTE(data->length >> 8);
    CMD_IF_RESPONSE_LEN_L = BYTE(data->length);
    CMD_IF_STATUS_LENGTH = status->length;
    if(data->last_part) {
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_VALIDATE_LAST;
        target = CMD_TARGET_NONE;
    } else {
        CMD_IF_HANDSHAKE_OUT = HANDSHAKE_VALIDATE_MORE;
    }    
}

int  CommandInterface :: fetch_task_items(IndexedList<PathObject*> &item_list)
{
    item_list.append(new ObjectMenuItem(this, "Run Command Cart", MENU_CMD_RUNCMDCART));  /* temporary item */
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

Message c_message_no_target      = {  9, true, (BYTE *)"NO TARGET" }; 
Message c_status_ok              = {  5, true, (BYTE *)"00,OK" };
Message c_status_unknown_command = { 18, true, (BYTE *)"21,UNKNOWN COMMAND" };
Message c_message_empty          = {  0, true, (BYTE *)"" };
