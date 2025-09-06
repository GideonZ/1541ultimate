#include "iec_interface.h"
#include "json.h"
#include "dump_hex.h"

IecInterface *iec_if = NULL;

IecInterface *IecInterface :: get_iec_interface(void)
{
    if (!iec_if) {
        iec_if = new IecInterface();
    }
    return iec_if;
}

extern uint8_t  _iec_code_b_start;
extern uint32_t _iec_code_b_size;

IecInterface :: IecInterface()
{
    if(!(getFpgaCapabilities() & CAPAB_HARDWARE_IEC))
        return;

    for(int i=0;i<MAX_SLOTS;i++) {
        slaves[i] = NULL;
    }
    available_slots = 0;

    get_patch_locations();
    program_processor();

    addressed_slave = NULL;
    atn = false;
    talking = false;
    enable = false;
    jiffy_load = false;
    jiffy_transfer = 0;
    queueToIec = xQueueCreate(2, sizeof(iec_closure_t));

    xTaskCreate( IecInterface :: start_task, "IEC Server", configMINIMAL_STACK_SIZE, this, PRIO_HW_SERVICE, &taskHandle );
}

IecInterface :: ~IecInterface()
{
    if (taskHandle) {
    	vTaskDelete(taskHandle);
    }

    HW_IEC_RESET_ENABLE = 0; // disable
}

int IecInterface :: register_slave(IecSlave *slave)
{
    for(int i=0; i < available_slots; i++) {
        if(!slaves[i]) {
            slaves[i] = slave;
            printf("Registered %s to slot %d.\n", slaves[i]->iec_identify(), i);
            return i;
        }
    }
    return -1;
}

void IecInterface :: unregister_slave(int slot)
{
    if ((slot < 0) || (slot >= MAX_SLOTS)) {
        return;
    }
    slaves[slot] = NULL;
}

void IecInterface :: program_processor(void)
{
    printf("IEC Processor found: Version = %b. Loading code...\n", HW_IEC_VERSION);
    HW_IEC_RESET_ENABLE = 0; // disable while programming
    int size = (int)&_iec_code_b_size;
    uint8_t *src = &_iec_code_b_start;
    uint8_t *dst = (uint8_t *)HW_IEC_CODE;
    for(int i=0; i<size; i++) {
        *(dst++) = *(src++);
    }
}

void IecInterface :: get_patch_locations(void)
{
    int size = (int)&_iec_code_b_size;
    uint8_t *src = &_iec_code_b_start;

    int found = 0;
    for(int i=0; i<MAX_SLOTS; i++) {
        talker_loc[i] = -1;
        listener_loc[i] = -1;
    }

    for(int i=0; i<size; i+=4) {
        // Each instruction is a 32 bit word 
        uint32_t word_read = (src[i] | src[i+1]<<8 | src[i+2]<<16 | src[i+3]<<24);

        int slot_id = word_read & 7;
        // Find replacer words
        if ((word_read & 0x1F8000F8) == 0x188000F0) {
            if (slot_id < MAX_SLOTS) {
                talker_loc[slot_id] = i;
                // printf("Talker %d @ %d\n", slot_id, i / 4);
                found++;
            }
        }
        if ((word_read & 0x1F8000F8) == 0x188000F8) {
            if (slot_id < MAX_SLOTS) {
                listener_loc[slot_id] = i;
                // printf("Listener %d @ %d\n", slot_id, i / 4);
                found++;
            }
        }
        if (found >= 2*MAX_SLOTS) {
            break;
        }
    }
    available_slots = found / 2;
}

void IecInterface :: set_slot_devnum(int slot, uint8_t dev)
{
    uint8_t *dst = (uint8_t *)HW_IEC_CODE;
    dst[listener_loc[slot]] = dev | 0x20;
    dst[talker_loc[slot]] = dev | 0x40;
}

void IecInterface :: configure(void)
{
    HW_IEC_RESET_ENABLE = 0;
    enable = false;
    for(int i=0; i < MAX_SLOTS; i++) {
        uint8_t addr = 0x1f;
        if (slaves[i]) {
            if (slaves[i]->is_enabled()) {
                addr = slaves[i]->get_address();
                enable = true;
            }
        }
        set_slot_devnum(i, addr);
    }
    if(enable) {
        HW_IEC_RESET_ENABLE = 1;
    }
}

void IecInterface :: reset(void)
{
    HW_IEC_RESET_ENABLE = 0;
    for(int i=0; i < MAX_SLOTS; i++) {
        if (slaves[i]) {
            slaves[i]->reset();
        }
    }
    configure();
}

void IecInterface :: start_task(void *a)
{
	IecInterface *iec = (IecInterface *)a;
	iec->task();
}

// this is actually the task
void IecInterface :: task()
{
    uint8_t data;
    iec_closure_t closure;
    BaseType_t gotSomething;

    while(1) {

        gotSomething = xQueueReceive(queueToIec, &closure, 2); // here is the vTaskDelay(2) that used to be here

        if (gotSomething == pdTRUE) {
            if (closure.func) {
                closure.func(closure.obj, closure.data);
            }
        }

        uint8_t a;
        for (int loopcnt = 0; loopcnt < 500; loopcnt++) {
            a = HW_IEC_RX_FIFO_STATUS;
            if (a & IEC_FIFO_EMPTY) {
                break;
            }

            data = HW_IEC_RX_DATA;

            if(a & IEC_FIFO_CTRL) {
                switch(data) {
                    case CTRL_READY_FOR_TX:
                    case CTRL_JIFFYLOAD:
                        DBGIEC(".RTS.");
                        jiffy_load = (data == CTRL_JIFFYLOAD);
                        if (addressed_slave) {
                            addressed_slave->talk();
                            HW_IEC_TX_FIFO_RELEASE = 1;
                            talking = true;
                        } else {
                            HW_IEC_TX_CTRL = IEC_CMD_TX_TERM;
                        }
                        break;

                    case CTRL_EOI:
                        DBGIEC(".EOI]\n");
                        if (addressed_slave) {
                            addressed_slave->push_ctrl(SLAVE_CMD_EOI);
                        }
                        addressed_slave = NULL;
                        break;

                    case CTRL_ATN_BEGIN:
                        DBGIEC("\n[ATN.");
                        atn = true;
                        talking = false;
                        break;

                    case CTRL_ATN_END:
                        DBGIEC(".ATN>");
                        atn = false;
                        break;

                    case CTRL_BYTE_TXED:
                        DBGIEC("o");
                        if (addressed_slave) {
                            addressed_slave->pop_data();
                        }
                        break;

                    case CTRL_DEV1:
                        DBGIEC(".Slave0.");
                        addressed_slave = slaves[0];
                        if (addressed_slave) {
                            addressed_slave->push_ctrl(SLAVE_CMD_ATN);
                        }
                        break;
                    
                    case CTRL_DEV2:
                        DBGIEC(".Slave1.");
                        addressed_slave = slaves[1];
                        if (addressed_slave) {
                            addressed_slave->push_ctrl(SLAVE_CMD_ATN);
                        }
                        break;

                    case CTRL_DEV3:
                        DBGIEC(".Slave2.");
                        addressed_slave = slaves[2];
                        if (addressed_slave) {
                            addressed_slave->push_ctrl(SLAVE_CMD_ATN);
                        }
                        break;

                    default:
                        DBGIECV(".<%b>.", data);
                        break;
                }
            } else {
                if(atn) {
                    DBGIECV(".<%b>.", data);

                    if(data >= 0x60) {  // workaround for passing of wrong atn codes talk/untalk
                        if (addressed_slave) {
                            DBGIECV(".CMD(%b).", data);
                            addressed_slave->push_ctrl(data);
                        }
                    }
                } else {
                    DBGIECV(".%b.", data);
                    if (addressed_slave) {
                        addressed_slave->push_data(data);
                    }
                }
            }
        }

        t_channel_retval st;

        jiffy_transfer = 0;
        if(talking) {
            while(1) {
                if (jiffy_load) {
                    // Simply spin while fifo is full
                    while(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)
                        ;
                } else {
                    // Exit loop when fifo is full
                    if(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)
                        break;
                }

                if (addressed_slave) {
                    st = addressed_slave->prefetch_data(data);
                } else {
                    st = IEC_READ_ERROR;
                }

                if(st == IEC_OK) {
                    HW_IEC_TX_DATA = data;
                    jiffy_transfer++;
#if IECDEBUG > 2
                    outbyte(data < 0x20?'.':data);
#endif
                } else if(st == IEC_LAST) {
                    HW_IEC_TX_LAST = data;
                    jiffy_transfer++;
#if IECDEBUG > 2
                    outbyte(data < 0x20?'.':data);
                    DBGIEC("[LAST]");
#endif
                    talking = false;
                    break;
                } else if(st == IEC_BUFFER_END) {
                    DBGIEC("[BEND]");
                    break;
                } else {
                    printf("Talk Error = %d\n", st);
                    HW_IEC_TX_CTRL = IEC_CMD_TX_TERM;
                    talking = false;
                    break;
                }
            }
#if IECDEBUG > 2
            outbyte('\'');
#endif
        }
        if (jiffy_load) {
            if (addressed_slave) {
                addressed_slave->pop_more(jiffy_transfer);
            } else {
                jiffy_load = false;
            }
        }
    }
}

void IecInterface :: run_from_iec(iec_closure_t *c)
{
    xQueueSend(queueToIec, c, 2000);
}


void IecInterface :: info(StreamTextLog &b)
{
    if (!iec_if) {
        return;
    }

    char buffer[64];
    b.format("SoftwareIEC:  %s\n", iec_if->enable ? "Enabled" : "Disabled");
    for(int i=0;i<MAX_SLOTS;i++) {
        if (iec_if->slaves[i]) {
            if (iec_if->slaves[i]->is_enabled()) {
                b.format("  %s (Bus ID: %d)\n", iec_if->slaves[i]->iec_identify(), iec_if->slaves[i]->get_address());
            } else {
                b.format("  %s (Disabled)\n", iec_if->slaves[i]->iec_identify());
            }
            iec_if->slaves[i]->info(b);
        }
    }
    b.format("\n");
}

void IecInterface :: info(JSON_List *lst)
{
    if (!iec_if) {
        return;
    }

    for(int i=0;i<MAX_SLOTS;i++) {
        if (iec_if->slaves[i]) {
            JSON_Object *new_obj = JSON::Obj();
            new_obj->add("bus_id", iec_if->slaves[i]->get_address());
            new_obj->add("enabled", iec_if->slaves[i]->is_enabled());
            iec_if->slaves[i]->info(new_obj);
            lst->add(JSON::Obj()->add(iec_if->slaves[i]->iec_identify(), new_obj));
        }
    }
}

void IecInterface :: info(Message& msg, int& offs)
{
    if (!iec_if) {
        return;
    }

    for(int i=0;i<MAX_SLOTS;i++) {
        if (iec_if->slaves[i]) {
            msg.message[offs++] = iec_if->slaves[i]->get_type();
            msg.message[offs++] = iec_if->slaves[i]->get_address();
            msg.message[offs++] = iec_if->slaves[i]->is_enabled() ? 1 : 0;
            msg.message[0] ++;
            offs+= 3;
        }            
    }
}

void IecInterface :: master_open_file(int device, int channel, const char *filename, bool write)
{
    printf("Open '%s' on device '%d', channel %d\n", filename, device, channel);
	HW_IEC_TX_FIFO_RELEASE = 1;
    HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
    HW_IEC_TX_DATA = 0x20 | (((uint8_t)device) & 0x1F); // listen!
    HW_IEC_TX_DATA = 0xF0 | (((uint8_t)channel) & 0x0F); // open on channel x
    HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_TX;

    int len = strlen(filename);
    for (int i=0;i<len;i++) {
        while(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)
            vTaskDelay(2);
        if (i == (len-1)) {
            HW_IEC_TX_LAST = (uint8_t)filename[i]; // EOI
        } else {
        	HW_IEC_TX_DATA = (uint8_t)filename[i];
        }
    }
    if(!write) {
        while(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_EMPTY))
            vTaskDelay(2);
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x3F; // unlisten
        HW_IEC_TX_CTRL = IEC_CMD_ATN_RELEASE;
        // and talk!
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x40 | (((uint8_t)device) & 0x1F); // talk
        HW_IEC_TX_DATA = 0x60 | (((uint8_t)channel) & 0x0F); // open channel
        HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_RX;
    }
}        

void flush_rx(void)
{
    uint8_t a;
    do {
        a = HW_IEC_RX_FIFO_STATUS;
        if (a & IEC_FIFO_EMPTY) {
            break;
        }
        uint8_t data = HW_IEC_RX_DATA;
        if (a & IEC_FIFO_CTRL) {
            DBGIECV("<%02x>", data);
        } else {
            DBGIECV("[%02x]", data);
        }
    }
    while(1);
}

bool IecInterface :: master_send_cmd(int device, uint8_t *cmd, int length)
{
    //printf("Send command on device '%d' [%s]\n", device, cmd);
    flush_rx();
#if IECDEBUG > 1
    dump_hex_relative(cmd, length);
#endif
	HW_IEC_TX_FIFO_RELEASE = 1;
    HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
    HW_IEC_TX_DATA = 0x20 | (((uint8_t)device) & 0x1F); // listen!
    HW_IEC_TX_DATA = 0x6F;
    HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_TX;

    int timeout = 0;
    for (int i=0;i<length;i++) {
        while(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL) {
            timeout++;
            if (timeout > 50) {
                printf("Timeout on TX FIFO\n");
                return false;
            }
            vTaskDelay(2);
        }
        if (i == (length-1)) {
            HW_IEC_TX_LAST = cmd[i];
        } else {
            HW_IEC_TX_DATA = cmd[i];
        }
    }
    timeout = 0;
    while(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_EMPTY)) {
        timeout++;
        if (timeout > 50) {
            printf("Timeout on TX FIFO\n");
            return false;
        }
        vTaskDelay(2);
    }

    HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
    HW_IEC_TX_DATA = 0x3F; // unlisten
    HW_IEC_TX_CTRL = IEC_CMD_ATN_RELEASE;

    timeout = 0;
    while(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_EMPTY)) {
        timeout++;
        if (timeout > 50) {
            printf("Timeout on TX FIFO\n");
            return false;
        }
        vTaskDelay(2);
    }
    flush_rx();
    return true;
}

void IecInterface :: master_read_status(int device)
{
    printf("Reading status channel from device %d\n", device);
	HW_IEC_TX_FIFO_RELEASE = 1;
    HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
    HW_IEC_TX_DATA = 0x40 | (((uint8_t)device) & 0x1F); // listen!
    HW_IEC_TX_DATA = 0x6F; // channel 15
    HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_RX;
}

bool IecInterface :: run_drive_code(int device, uint16_t addr, uint8_t *code, int length)
{
    printf("Load drive code. Length = %d\n", length);
    uint8_t buffer[40];
    uint16_t address = addr;
    int size;
    strcpy((char*)buffer, "M-W");
    while(length > 0) {
        size = (length > 32)?32:length;
        buffer[3] = (uint8_t)(address & 0xFF);
        buffer[4] = (uint8_t)(address >> 8);
        buffer[5] = (uint8_t)(size);
        for(int i=0;i<size;i++) {
            buffer[6+i] = *(code++);
        }
        printf(".");
        if(!master_send_cmd(device, buffer, size+6))
            return false;
        length -= size;
        address += size;
    }
    strcpy((char*)buffer, "M-E");
    buffer[3] = (uint8_t)(addr & 0xFF);
    buffer[4] = (uint8_t)(addr >> 8);
    printf("\n");
    return master_send_cmd(device, buffer, 5);
}
