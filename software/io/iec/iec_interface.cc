#include "iec_interface.h"
#include "json.h"

IecInterface *iec_if;

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

    get_patch_locations();
    program_processor();

    addressed_slave = NULL;
    atn = false;
    talking = false;
    wait_irq = false;
    enable = false;

    queueGuiToIec = xQueueCreate(2, sizeof(char *));

    xTaskCreate( IecInterface :: start_task, "IEC Server", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 2, &taskHandle );
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
    printf("IEC Processor found: Version = %b. Loading code...", HW_IEC_VERSION);
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
    char *pathstring;
    BaseType_t gotSomething;

    while(1) {
        if(wait_irq) {
            if (HW_IEC_IRQ & 0x01) {
                // get_warp_data();
            }
            continue;
        }

        gotSomething = xQueueReceive(queueGuiToIec, &pathstring, 2); // here is the vTaskDelay(2) that used to be here

/*
        if (gotSomething == pdTRUE) {
            if (!pathstring) {
                start_warp_iec();
            } else {
                IecPartition *p = vfs->GetPartition(0);
                p->cd(pathstring);
                delete[] pathstring;
            }
        }
*/
        uint8_t a;
        for (int loopcnt = 0; loopcnt < 500; loopcnt++) {
            a = HW_IEC_RX_FIFO_STATUS;
            if (a & IEC_FIFO_EMPTY) {
                break;
            }

            data = HW_IEC_RX_DATA;

            if(a & IEC_FIFO_CTRL) {
                switch(data) {
                    case WARP_START_RX:
                        DBGIEC("[WARP:START.");
                        HW_IEC_TX_DATA = 0x00; // handshake and wait for IRQ
                        wait_irq = true;
                        break;

                    case WARP_BLOCK_END:
                        DBGIEC(".WARP:OK]");
                        break;

                    case WARP_RX_ERROR:
                        DBGIEC(".WARP:ERR]");
                        // get_warp_error();
                        break;

                    case WARP_ACK:
                        DBGIEC(".WARP:ACK.");
                        break;

                    case CTRL_READY_FOR_TX:
                        DBGIEC(".RTS.");
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
                            addressed_slave->push_ctrl(data); // FIXME: Printer should and with 0x7
                            // FIXME: Drive should choose channel from lower nybble and forward
                            // upper 4 bits of command to channel
                        }
                    }
                } else {
                    DBGIECV(".%b.", data);
                    if (addressed_slave) {
                        addressed_slave->push_data(data);
                    }
                }
            }

            if (wait_irq) {
                break;
            }
        }

        t_channel_retval st;

        if(talking) {
            while(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)) {
                if (addressed_slave) {
                    st = addressed_slave->prefetch_data(data);
                } else {
                    st = IEC_READ_ERROR;
                }

                if(st == IEC_OK) {
                    HW_IEC_TX_DATA = data;
#if IECDEBUG > 2
                    outbyte(data < 0x20?'.':data);
#endif
                } else if(st == IEC_LAST) {
                    HW_IEC_TX_LAST = data;
#if IECDEBUG > 2
                    outbyte(data < 0x20?'.':data);
#endif
                    talking = false;
                    break;
                } else if(st == IEC_BUFFER_END) {
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
    }
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
