#include "integer.h"
extern "C" {
	#include "dump_hex.h"
    #include "small_printf.h"
}
#include "command_intf.h"
#include "poll.h"

// this global will cause us to run!
CommandInterface cmd_if;

void poll_command_interface(Event &ev)
{
    cmd_if.poll(ev);
}

CommandInterface :: CommandInterface()
{
    if(CAPABILITIES & CAPAB_COMMAND_INTF) {
        CMD_IF_SLOT_BASE = 0xFC;
        CMD_IF_SLOT_ENABLE = 1;
    
        poll_list.append(&poll_command_interface);
    
        dump_registers();
    
        response_buffer = (volatile BYTE *)(CMD_IF_RAM_BASE + (8*CMD_IF_RESPONSE_START));
        status_buffer   = (volatile BYTE *)(CMD_IF_RAM_BASE + (8*CMD_IF_STATUS_START));
        command_buffer  = (volatile BYTE *)(CMD_IF_RAM_BASE + (8*CMD_IF_COMMAND_START));
    
        sprintf((char *)response_buffer, "Ultimate-II V2.1");
        CMD_IF_RESPONSE_LEN_H = 0;
        CMD_IF_RESPONSE_LEN_L = 16;
        
        seen_handshake = 0;
    }
}

CommandInterface :: ~CommandInterface()
{
	poll_list.remove(&poll_command_interface);

    CMD_IF_SLOT_ENABLE = 0;
}

int CommandInterface :: poll(Event &e)
{
//    printf("Poll Command IF!\n");
    int length;
    BYTE handshake_in = (CMD_IF_HANDSHAKE_IN & 0x0F);

    if(handshake_in != seen_handshake) {
        length = int(CMD_IF_COMMAND_LEN_L) + (int(CMD_IF_COMMAND_LEN_H) << 8);
        printf("Command received:\n");
        dump_hex_relative((void *)command_buffer, length);
        seen_handshake = handshake_in;
    }
}

void CommandInterface :: dump_registers(void)
{
    printf("CMD_IF_SLOT_BASE       %b\n", CMD_IF_SLOT_BASE     );
    printf("CMD_IF_SLOT_ENABLE     %b\n", CMD_IF_SLOT_ENABLE   );
    printf("CMD_IF_HANDSHAKE_OUT   %b\n", CMD_IF_HANDSHAKE_OUT );
    printf("CMD_IF_HANDSHAKE_IN    %b\n", CMD_IF_HANDSHAKE_IN  );
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
