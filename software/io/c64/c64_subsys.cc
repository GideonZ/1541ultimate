/*
 * c64_subsys.cc
 *
 *  Created on: Jul 16, 2015
 *      Author: Gideon
 */

#include "c64.h"
#include "c64_subsys.h"
#include <ctype.h>

/* other external references */
extern uint8_t _binary_bootcrt_65_start;

cart_def boot_cart = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM };

static inline uint16_t le2cpu(uint16_t p)
{
	uint16_t out = (p >> 8) | (p << 8);
	return out;
}

C64_Subsys::C64_Subsys(C64 *machine)  : SubSystem(SUBSYSID_C64) {
	taskHandle = 0;
	c64 = machine;
	fm = FileManager :: getFileManager();
	xTaskCreate( C64_Subsys :: poll, "C64 Server", configMINIMAL_STACK_SIZE, machine, tskIDLE_PRIORITY + 1, &taskHandle );
}

C64_Subsys::~C64_Subsys() {
    if (taskHandle) {
    	vTaskDelete(taskHandle);
    }

}

int  C64_Subsys :: fetch_task_items(Path *path, IndexedList<Action *> &item_list)
{
	int count = 3;
	item_list.append(new Action("Reset C64", SUBSYSID_C64, MENU_C64_RESET));
	item_list.append(new Action("Reboot C64", SUBSYSID_C64, MENU_C64_REBOOT));
    item_list.append(new Action("Hard System Reboot", SUBSYSID_C64, MENU_C64_HARD_BOOT));
    //item_list.append(new Action("Boot Alternate FPGA", SUBSYSID_C64, MENU_C64_BOOTFPGA));
    //item_list.append(new Action("Save SID Trace", SUBSYSID_C64, MENU_C64_TRACE));

    if(fm->is_path_writable(path)) {
    	item_list.append(new Action("Save REU Memory", SUBSYSID_C64, MENU_C64_SAVEREU));
    	count ++;
        // item_list.append(new Action("Save Flash", SUBSYSID_C64, MENU_C64_SAVEFLASH));
    }
	return count;
}

void C64_Subsys :: poll(void *a)
{
	C64 *c64 = (C64 *)a;

	static uint8_t button_prev;
	while(1) {
		uint8_t buttons = ioRead8(ITU_IRQ_ACTIVE) & ITU_BUTTONS;
		if((buttons & ~button_prev) & ITU_BUTTON1) {
			c64->buttonPushSeen = true;
		}
		button_prev = buttons;
		vTaskDelay(5);
	}
}

void C64_Subsys :: restoreCart(void)
{
	while (C64_CARTRIDGE_ACTIVE) {
		vTaskDelay(2);
	}
	printf("Cart got disabled, now restoring.\n");
	c64->set_cartridge(NULL);
}


int C64_Subsys :: executeCommand(SubsysCommand *cmd)
{
	File *f;
	CachedTreeNode *po;
	uint32_t transferred;
	int ram_size;

    switch(cmd->functionID) {
    case C64_UNFREEZE:
		if(c64->client) { // we can't execute this yet
			c64->client->release_host(); // disconnect from user interface
			c64->client = 0;
		}
		c64->unfreeze(0, 0);
    	break;
    case MENU_C64_RESET:
		if(c64->client) { // we can't execute this yet
			c64->client->release_host(); // disconnect from user interface
			c64->client = 0;
		}
		c64->unfreeze(0, 0);
		c64->reset();
		break;
	case MENU_C64_REBOOT:
		if(c64->client) { // we can't execute this yet
			c64->client->release_host(); // disconnect from user interface
			c64->client = 0;
		}
		c64->unfreeze(0, 0);
		c64->init_cartridge();
		break;
	case C64_START_CART:
		if(c64->client) { // we can't execute this yet
			c64->client->release_host(); // disconnect from user interface
			c64->client = 0;
		}
		c64->unfreeze((cart_def *)cmd->mode, 1);
		break;
	case MENU_C64_SAVEREU:
        ram_size = 128 * 1024;
        ram_size <<= c64->cfg->get_value(CFG_C64_REU_SIZE);
        f = fm->fopen(cmd->path.c_str(), "memory.reu", FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS);
        if(f) {
            printf("Opened file successfully.\n");
            f->write((void *)REU_MEMORY_BASE, ram_size, &transferred);
            printf("written: %d...", transferred);
            fm->fclose(f);
        } else {
            printf("Couldn't open file..\n");
        }
        break;
    case MENU_C64_SAVEFLASH: // doesn't belong here, but i need it fast
        f = fm->fopen(cmd->path.c_str(), "flash_dump.bin", FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS);
        if(f) {
            int pages = c64->flash->get_number_of_pages();
            int page_size = c64->flash->get_page_size();
            uint8_t *page_buf = new uint8_t[page_size];
            for(int i=0;i<pages;i++) {
                c64->flash->read_page(i, page_buf);
                f->write(page_buf, page_size, &transferred);
            }
            delete[] page_buf;
            fm->fclose(f);
        } else {
            printf("Couldn't open file..\n");
        }
        break;
    case C64_DMA_LOAD:
    	f = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ);
    	if(f) {
        	dma_load(f, cmd->filename.c_str(), cmd->mode);
        	fm->fclose(f);
        }
    	break;
    case C64_DRIVE_LOAD:
    	dma_load(0, cmd->filename.c_str(), cmd->mode);
    	break;
    case MENU_C64_HARD_BOOT:
		c64->flash->reboot(0);
		break;
	default:
		break;
	}

    return 0;
}

int C64_Subsys :: dma_load(File *f, const char *name, uint8_t run_code, uint16_t reloc)
{
	// prepare DMA load
    if(c64->client) { // we are locked by a client, likely: user interface
    	c64->client->release_host(); // disconnect from user interface
    	c64->client = 0;
	}
    if(!c64->stopped) {
    	c64->stop();
    }

    C64_POKE(0x162, run_code);

	int len = strlen(name);
	if (len > 30)
		len = 30;

	for (int i=0; i < len; i++) {
        C64_POKE(0x165+i, toupper(name[i]));
    }
    C64_POKE(0x164, len);
    C64_POKE(2, 0x80); // initial boot cart handshake

    boot_cart.custom_addr = (void *)&_binary_bootcrt_65_start;
    c64->unfreeze(&boot_cart, 1);

	// perform DMA load
    // First, check if we have file access
    uint32_t transferred;
	uint16_t load_address = 0;
    if (f) {
        f->read(&load_address, 2, &transferred);
        if(transferred != 2) {
            printf("Can't read from file..\n");
            return -1;
        }
        load_address = le2cpu(load_address); // make sure we can interpret the word correctly (endianness)
        printf("Load address: %4x...", load_address);
    } else {
    	wait_ms(100);
    }
    int block = 510;

    if(reloc)
    	load_address = reloc;

    int max_length = 65536 - int(load_address); // never exceed $FFFF

	// handshake with boot cart
    c64->stop(false);
	C64_POKE(0x163, c64->cfg->get_value(CFG_C64_DMA_ID));    // drive number for printout

	C64_POKE(2, 0x40);  // signal cart ready for DMA load

	uint8_t dma_load_buffer[512];

	if ( !(run_code & RUNCODE_REAL_BIT) ) {

        int timeout = 0;
        while(C64_PEEK(2) != 0x01) {
        	c64->resume();
            timeout++;
            if(timeout == 60) {
                c64->init_cartridge();
                return -1;
            }
            wait_ms(25);
            printf("_");
            c64->stop(false);
        }
        printf("Now loading...");
        uint8_t *dest = (uint8_t *)(C64_MEMORY_BASE + load_address);

        /* Now actually load the file */
        int total_trans = 0;
        while (max_length > 0) {
        	f->read(dma_load_buffer, block, &transferred);
        	total_trans += transferred;
        	for (int i=0;i<transferred;i++) {
        		*(dest++) = dma_load_buffer[i];
        	}
        	if (transferred < block) {
        		break;
        	}
        	max_length -= transferred;
        	block = 512;
        }
        uint16_t end_address = load_address + total_trans;
        printf("DMA load complete: $%4x-$%4x Run Code: %b\n", load_address, end_address, run_code);

        C64_POKE(2, 0); // signal DMA load done
        C64_POKE(0x0162, run_code);
        C64_POKE(0x00BA, c64->cfg->get_value(CFG_C64_DMA_ID));    // fix drive number

        C64_POKE(0x002D, end_address);
        C64_POKE(0x002E, end_address >> 8);
        C64_POKE(0x002F, end_address);
        C64_POKE(0x0030, end_address >> 8);
        C64_POKE(0x0031, end_address);
        C64_POKE(0x0032, end_address >> 8);
        C64_POKE(0x00AE, end_address);
        C64_POKE(0x00AF, end_address >> 8);

        C64_POKE(0x0090, 0x40); // Load status
        C64_POKE(0x0035, 0);    // FRESPC
        C64_POKE(0x0036, 0xA0);
    }

    printf("Resuming..\n");
    c64->resume();

    restoreCart();
    return 0;
}

bool C64_Subsys :: write_vic_state(File *f)
{
    uint32_t transferred;
    uint8_t mode = C64_MODE;
    C64_MODE = 0;

    // find bank
    uint8_t bank = 3 - (c64->cia_backup[1] & 0x03);
    uint32_t addr = C64_MEMORY_BASE + (uint32_t(bank) << 14);
    if(bank == 0) {
        f->write((uint8_t *)C64_MEMORY_BASE, 0x0400, &transferred);
        f->write(c64->screen_backup, 0x0400, &transferred);
        f->write(c64->ram_backup, 0x0800, &transferred);
        f->write((uint8_t *)(C64_MEMORY_BASE + 0x1000), 0x3000, &transferred);
    } else {
        f->write((uint8_t *)addr, 16384, &transferred);
    }
    f->write(c64->color_backup, 0x0400, &transferred);
    f->write(c64->vic_backup, NUM_VICREGS, &transferred);

    C64_MODE = mode;
    return true;
}
