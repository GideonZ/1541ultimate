/*
 * c64_subsys.cc
 *
 *  Created on: Jul 16, 2015
 *      Author: Gideon
 */

#include "c64.h"
#include "c64_crt.h"
#include "c64_subsys.h"
#include <ctype.h>
#include "init_function.h"
#include "userinterface.h"
#include "u64.h"
#include "c1541.h"
#include "endianness.h"
#if U64 == 2
#include "wifi.h"
#endif

#define C64_BOOTCRT_DOSYNC    0x014F
#define C64_BOOTCRT_RUNCODE   0x0172
#define C64_BOOTCRT_DRIVENUM  0x0173
#define C64_BOOTCRT_NAMELEN   0x0174
#define C64_BOOTCRT_NAME      0x0175
#define C64_BOOTCRT_HANDSHAKE 0x02
#define C64_BOOTCRT_JUMPADDR  0x00AA

/* other external references */
extern uint8_t _bootcrt_65_start;
extern uint8_t _bootcrt_65_end;

cart_def boot_cart; // static => initialized with all zeros.

static void initBootCart(void *object, void *param)
{
    int size = (int)&_bootcrt_65_end - (int)&_bootcrt_65_start;
    boot_cart.custom_addr = new uint8_t[8192];
    boot_cart.length = size;
    boot_cart.type = CART_TYPE_8K;

    memcpy(boot_cart.custom_addr, &_bootcrt_65_start, size);
    printf("%d bytes copied into boot_cart.\n", size);
}
InitFunction bootCart_initializer("Boot Cart", initBootCart, NULL, NULL);

C64_Subsys::C64_Subsys(C64 *machine)  : SubSystem(SUBSYSID_C64) {
	taskHandle = 0;
	c64 = machine;
	fm = FileManager :: getFileManager();
	taskHandle = 0;

	taskCategory = TasksCollection :: getCategory("C64 Machine", SORT_ORDER_C64);
}

C64_Subsys::~C64_Subsys() {
    if (taskHandle) {
    	vTaskDelete(taskHandle);
    }

}

void C64_Subsys :: create_task_items(void)
{
    myActions.reset    = new Action("Reset C64", SUBSYSID_C64, MENU_C64_RESET);
    myActions.reboot   = new Action("Reboot C64", SUBSYSID_C64, MENU_C64_REBOOT);
    myActions.clearmem = new Action("Reboot (Clr Mem)", SUBSYSID_C64, MENU_C64_CLEARMEM);
    myActions.powercyc = new Action("Power Cycle", SUBSYSID_C64, MENU_C64_POWERCYCLE);
    myActions.off      = new Action("Power OFF", SUBSYSID_C64, MENU_C64_POWEROFF);
    myActions.pause    = new Action("Pause",  SUBSYSID_C64, MENU_C64_PAUSE);
    myActions.resume   = new Action("Resume", SUBSYSID_C64, MENU_C64_RESUME);
    myActions.savereu  = new Action("Save REU Memory", SUBSYSID_C64, MENU_C64_SAVEREU);
    myActions.savemem  = new Action("Save C64 Memory", SUBSYSID_C64, MENU_U64_SAVERAM);
    myActions.save_crt = new Action("Save Cartridge", SUBSYSID_C64, MENU_C64_SAVE_CARTRIDGE);
    myActions.savemp3a = new Action("Save MP3 Drv A", SUBSYSID_C64, MENU_C64_SAVE_MP3_DRV_A);
    myActions.savemp3b = new Action("Save MP3 Drv B", SUBSYSID_C64, MENU_C64_SAVE_MP3_DRV_B);
    myActions.savemp3c = new Action("Save MP3 Drv C", SUBSYSID_C64, MENU_C64_SAVE_MP3_DRV_C);
    myActions.savemp3d = new Action("Save MP3 Drv D", SUBSYSID_C64, MENU_C64_SAVE_MP3_DRV_D);
    myActions.measure  = new Action("Measure Cart Bus", SUBSYSID_C64, MENU_MEASURE_TIMING);

    taskCategory->append(myActions.reset);
    taskCategory->append(myActions.reboot);
#if U64
    taskCategory->append(myActions.clearmem);
    taskCategory->append(myActions.off);
#endif
#if U64 == 2
    taskCategory->append(myActions.powercyc);
#endif
#if DEVELOPER > 0
    taskCategory->append(myActions.pause);
    taskCategory->append(myActions.resume);
    taskCategory->append(myActions.measure);
#endif
#if U64
    taskCategory->append(myActions.savemem);
#endif
    taskCategory->append(myActions.savereu);
    taskCategory->append(myActions.save_crt);
    taskCategory->append(myActions.savemp3a);
    taskCategory->append(myActions.savemp3b);
    taskCategory->append(myActions.savemp3c);
    taskCategory->append(myActions.savemp3d);
}

void C64_Subsys :: update_task_items(bool writablePath, Path *p)
{
    if (C64 :: isMP3RamDrive(0) > 0) myActions.savemp3a->show(); else myActions.savemp3a->hide();
    if (C64 :: isMP3RamDrive(1) > 0) myActions.savemp3b->show(); else myActions.savemp3b->hide();
    if (C64 :: isMP3RamDrive(2) > 0) myActions.savemp3c->show(); else myActions.savemp3c->hide();
    if (C64 :: isMP3RamDrive(3) > 0) myActions.savemp3d->show(); else myActions.savemp3d->hide();

    if (C64_CRT :: is_valid()) {
        myActions.save_crt->show();
    } else {
        myActions.save_crt->hide();
    }

    if (writablePath) {
#if U64
        myActions.savemem  ->enable();
#endif
        myActions.savereu  ->enable();
        myActions.save_crt ->enable();
        myActions.savemp3a ->enable();
        myActions.savemp3b ->enable();
        myActions.savemp3c ->enable();
        myActions.savemp3d ->enable();
    } else {
#if U64
        myActions.savemem  ->disable();
#endif
        myActions.savereu  ->disable();
        myActions.save_crt ->disable();
        myActions.savemp3a ->disable();
        myActions.savemp3b ->disable();
        myActions.savemp3c ->disable();
        myActions.savemp3d ->disable();
    }
}


void C64_Subsys :: restoreCart(void)
{
	for (int i=0; (i<500) && (C64_CARTRIDGE_ACTIVE); i++) {
		vTaskDelay(2);
	}
	if (C64_CARTRIDGE_ACTIVE) {
	    printf("Error.. cart did not get disabled.\n");
	    c64->init_cartridge(); // hard reset
	} else {
	    printf("Cart got disabled, now restoring.\n");
	    c64->set_cartridge(NULL);
        C64_CARTRIDGE_KILL = 2; // Force update
        C64_CARTRIDGE_KILL = 2; // Force update
	}
}


SubsysResultCode_e C64_Subsys::executeCommand(SubsysCommand *cmd)
{
    File *f = 0;
    FRESULT res;
    CachedTreeNode *po;
    uint32_t transferred;
    int ram_size;
    char buffer[64] = "memory";
    uint8_t *pb;
    SubsysResultCode_e result = SSRET_OK;

    switch (cmd->functionID) {
        case C64_PUSH_BUTTON:
            c64->setButtonPushed();
            break;
        case C64_UNFREEZE:
            if (c64->client) { // we can't execute this yet
                c64->client->release_host(); // disconnect from user interface
                c64->client = 0;
            }
            c64->unfreeze();
            break;
        case MENU_C64_RESET:
            if (c64->client) { // we can't execute this yet
                c64->client->release_host(); // disconnect from user interface
                c64->client = 0;
            }
            c64->unfreeze();
            c64->reset();
            break;

        case MENU_C64_PAUSE:
            c64->stop(false);
            break;

        case MENU_C64_RESUME:
            c64->resume();
            break;

        case MENU_C64_CLEARMEM:
#if U64
            if (c64->client) { // we can't execute this yet
                c64->client->release_host(); // disconnect from user interface
                c64->client = 0;
            }
            c64->clear_ram();
            c64->start_cartridge(NULL);
            break;
#endif

        case MENU_C64_POWERCYCLE:
#if U64 == 2
            wifi_machine_reboot();
#else
            result = SSRET_NOT_IMPLEMENTED;
#endif
            break;
        case MENU_C64_POWEROFF:
#if U64 == 2
            wifi_machine_off();
#elif U64 == 1
            U64_POWER_REG = 0x2B;
            U64_POWER_REG = 0xB2;
            U64_POWER_REG = 0x2B;
            U64_POWER_REG = 0xB2;
#else
            result = SSRET_NOT_IMPLEMENTED;
#endif
            break;

        case MENU_C64_REBOOT:
            if (c64->client) { // we can't execute this yet
                c64->client->release_host(); // disconnect from user interface
                c64->client = 0;
            }
            c64->start_cartridge(NULL);
            break;

        case C64_START_CART:
            if (c64->client) { // we can't execute this yet
                c64->client->release_host(); // disconnect from user interface
                c64->client = 0;
            }
            c64->start_cartridge((cart_def *)cmd->mode);
            break;

        case MENU_C64_SAVEREU:
            ram_size = 128 * 1024;
            ram_size <<= c64->cfg->get_value(CFG_C64_REU_SIZE);

            if (cmd->user_interface->string_box("Save REU memory as..", buffer, 22) > 0) {
                fix_filename(buffer);
                set_extension(buffer, ".reu", 32);

                res = create_file_ask_if_exists(fm, cmd->user_interface, cmd->path.c_str(), buffer, &f);
                if (res == FR_OK) {
                    printf("Opened file successfully.\n");

                    uint32_t reu_sizes[] =
                            { 0x20000, 0x40000, 0x80000, 0x100000, 0x200000, 0x400000, 0x800000, 0x1000000 };

                    uint32_t current_reu_size = reu_sizes[c64->cfg->get_value(CFG_C64_REU_SIZE)];
                    uint32_t sectors = (current_reu_size >> 9);
                    uint32_t secs_per_step = (sectors + 31) >> 5;
                    uint32_t bytes_per_step = secs_per_step << 9;
                    uint32_t bytes_written;
                    uint32_t remain = current_reu_size;
                    uint8_t *src;
                    transferred = 0;

                    cmd->user_interface->show_progress("Saving REU file..", 32);
                    src = (uint8_t *)REU_MEMORY_BASE;

                    while (remain != 0) {
                        f->write(src, bytes_per_step, &bytes_written);
                        transferred += bytes_written;
                        cmd->user_interface->update_progress(NULL, 1);
                        remain -= bytes_per_step;
                        src += bytes_per_step;
                    }
                    cmd->user_interface->hide_progress();

                    printf("written: %d...", transferred);
                    sprintf(buffer, "Bytes saved: %d ($%8x)", transferred, transferred);
                    cmd->user_interface->popup(buffer, BUTTON_OK);

                    fm->fclose(f);
                } else {
                    printf("Couldn't open file..\n");
                    cmd->user_interface->popup(FileSystem::get_error_string(res), BUTTON_OK);
                }
            }
            break;

        case MENU_U64_SAVERAM:
            ram_size = 64 * 1024;
            if (cmd->user_interface->string_box("Save RAM as..", buffer, 22) > 0) {
                fix_filename(buffer);
                set_extension(buffer, ".bin", 32);

                res = create_file_ask_if_exists(fm, cmd->user_interface, cmd->path.c_str(), buffer, &f);
                if (res == FR_OK) {
                    printf("Opened file successfully.\n");

                    pb = new uint8_t[ram_size];
                    c64->get_all_memory(pb);

                    uint32_t bytes_written;
                    f->write(pb, ram_size, &bytes_written);

                    printf("written: %d...", bytes_written);
                    sprintf(buffer, "bytes saved: %d ($%8x)", bytes_written, bytes_written);
                    cmd->user_interface->popup(buffer, BUTTON_OK);

                    fm->fclose(f);
                    delete[] pb;
                } else {
                    printf("Couldn't open file..\n");
                    cmd->user_interface->popup(FileSystem::get_error_string(res), BUTTON_OK);
                }
            }
            break;

        case MENU_C64_SAVE_CARTRIDGE:
            buffer[0] = 0;
            if (cmd->user_interface->string_box("Save Cartridge as..", buffer, 28) > 0) {
                fix_filename(buffer);
                set_extension(buffer, ".crt", 32);

                res = create_file_ask_if_exists(fm, cmd->user_interface, cmd->path.c_str(), buffer, &f);
                if (res == FR_OK) {
                    SubsysResultCode_e retval = C64_CRT::save_crt(f);
                    fm->fclose(f);
                    if (retval != SSRET_OK) {
                        cmd->user_interface->popup(SubsysCommand::error_string(retval), BUTTON_OK);
                    }
                }
            }
            break;

        case MENU_C64_SAVEFLASH: // doesn't belong here, but i need it fast
            res = create_file_ask_if_exists(fm, cmd->user_interface, cmd->path.c_str(), "flash_dump.bin", &f);

            if (res == FR_OK) {
                int pages = c64->flash->get_number_of_pages();
                int page_size = c64->flash->get_page_size();
                uint8_t *page_buf = new uint8_t[page_size];
                for (int i = 0; i < pages; i++) {
                    c64->flash->read_page(i, page_buf);
                    f->write(page_buf, page_size, &transferred);
                }
                delete[] page_buf;
                fm->fclose(f);
            } else {
                printf("Couldn't open file..\n");
            }
            break;

        case MENU_MEASURE_TIMING:
            measure_timing(cmd->path.c_str());
            break;

        case MENU_MEASURE_TIMING_API:
            c64->measure_timing((uint8_t *)cmd->buffer);
            break;

        case C64_DMA_LOAD:
            res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &f);
            if (res == FR_OK) {
                dma_load(f, NULL, 0, cmd->filename.c_str(), cmd->mode, c64->cfg->get_value(CFG_C64_DMA_ID));
                fm->fclose(f);
            } else {
                result = SSRET_CANNOT_OPEN_FILE;
            }
            break;

#ifndef RECOVERYAPP
        case C64_DMA_LOAD_MNT:
            res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &f);
            if (res == FR_OK) {
                dma_load(f, NULL, 0, cmd->filename.c_str(), cmd->mode, c1541_A->get_current_iec_address());
                fm->fclose(f);
            } else {
                result = SSRET_CANNOT_OPEN_FILE;
            }
            break;
#endif
        case C64_DMA_LOAD_RAW:
            res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &f);
            if (res == FR_OK) {
                dma_load_raw(f);
                fm->fclose(f);
            } else {
                result = SSRET_CANNOT_OPEN_FILE;
            }
            break;
        case C64_DRIVE_LOAD:
            dma_load(0, NULL, 0, cmd->filename.c_str(), cmd->mode, cmd->path.c_str()[0] & 0x1F);
            if (cmd->user_interface) {
                cmd->user_interface->menu_response_to_action = MENU_HIDE;
            }
            break;
        case C64_DMA_BUFFER:
            dma_load(0, (const uint8_t *)cmd->buffer, cmd->bufferSize, cmd->filename.c_str(), cmd->mode,
                    c64->cfg->get_value(CFG_C64_DMA_ID));
            break;
        case C64_DMA_RAW_WRITE:
            dma_load_raw_buffer((uint16_t)cmd->mode, (uint8_t *)cmd->buffer, cmd->bufferSize, 0);
            break;
        case C64_DMA_RAW_READ:
            dma_load_raw_buffer((uint16_t)cmd->mode, (uint8_t *)cmd->buffer, cmd->bufferSize, 1);
            break;
        case C64_STOP_COMMAND:
            c64->stop(false);
            break;
        case MENU_C64_HARD_BOOT:
            c64->flash->reboot(0);
            break;
        case C64_READ_FLASH:
            switch (cmd->mode) {
                case FLASH_CMD_PAGESIZE:
                    *(int*)cmd->buffer = c64->flash->get_page_size();
                    break;
                case FLASH_CMD_NOPAGES:
                    *(int*)cmd->buffer = c64->flash->get_number_of_pages();
                    break;
                default:
                    if (cmd->bufferSize >= c64->flash->get_page_size())
                        c64->flash->read_page(cmd->mode - FLASH_CMD_GETPAGE, cmd->buffer);
            }
            break;
        case C64_SET_KERNAL:
            c64->enable_kernal((uint8_t*)cmd->buffer);
            // c64->reset();
            break;
        case MENU_C64_SAVE_MP3_DRV_A:
            case MENU_C64_SAVE_MP3_DRV_B:
            case MENU_C64_SAVE_MP3_DRV_C:
            case MENU_C64_SAVE_MP3_DRV_D:
            {
            int devNo = cmd->functionID - MENU_C64_SAVE_MP3_DRV_A;
            int ftype = C64::isMP3RamDrive(devNo);
            uint8_t* reu = (uint8_t *)(REU_MEMORY_BASE);
            int expSize = 1;
            const char* extension = 0;

            if (ftype == 1541) {
                expSize = 174848;
                extension = ".D64";
            }
            if (ftype == 1571) {
                expSize = 2 * 174848;
                extension = ".D71";
            }
            if (ftype == 1581) {
                expSize = 819200;
                extension = ".D81";
            }
            if (ftype == DRVTYPE_MP3_DNP) {
                expSize = C64::getSizeOfMP3NativeRamdrive(devNo);
                extension = ".DNP";
            }

            uint8_t ramBase = reu[0x7dc7 + devNo];
            uint8_t* srcAddr = reu + (((uint32_t)ramBase) << 16);

            if (cmd->user_interface->string_box("Save RAMDISK as..", buffer, 22) > 0) {
                fix_filename(buffer);
                set_extension(buffer, extension, 32);

                res = create_file_ask_if_exists(fm, cmd->user_interface, cmd->path.c_str(), buffer, &f);
                if (res == FR_OK) {
                    printf("Opened file successfully.\n");
                    uint32_t bytes_written;

                    if (ftype == 1571) {
                        f->write(srcAddr, expSize / 2, &bytes_written);
                        uint32_t tmp;
                        f->write(srcAddr + 700 * 256, expSize / 2, &tmp);
                        bytes_written += tmp;
                    } else {
                        f->write(srcAddr, expSize, &bytes_written);
                    }
                    printf("written: %d...", bytes_written);
                    sprintf(buffer, "bytes saved: %d ($%8x)", bytes_written, bytes_written);
                    cmd->user_interface->popup(buffer, BUTTON_OK);

                    fm->fclose(f);
                } else {
                    printf("Couldn't open file..\n");
                    cmd->user_interface->popup(FileSystem::get_error_string(res), BUTTON_OK);
                }
            }
        }
            break;

        default:
            break;
    }

    return result;
}

int C64_Subsys :: dma_load_raw(File *f)
{
	bool i_stopped_it = false;
	if (c64->client) {
    	c64->client->release_host(); // disconnect from user interface
    	c64->release_ownership();
	}
	if(!c64->isFrozen) {
		c64->stop(false);
        C64_MODE = MODE_NORMAL;
		i_stopped_it = true;
	}
	int bytes = load_file_dma(f, 0);

	if (i_stopped_it) {
		c64->resume();
	}
	return bytes;
}

int C64_Subsys :: dma_load_raw_buffer(uint16_t offset, uint8_t *buffer, int length, int rw)
{
    bool i_stopped_it = false;
    if (c64->client) {
        c64->client->release_host(); // disconnect from user interface
        c64->release_ownership();
    }
    if(!c64->is_stopped()) {
        c64->stop(false);
        i_stopped_it = true;
    }

    volatile uint8_t *dest = (volatile uint8_t *)(C64_MEMORY_BASE + offset);

    if (rw) {
        memcpy(buffer, (void *)dest, length);
    } else {
        memcpy((void *)dest, buffer, length);
    }

    if (i_stopped_it) {
        c64->resume();
    }
	return length;
}

int C64_Subsys :: dma_load(File *f, const uint8_t *buffer, const int bufferSize,
		const char *name, uint8_t run_code, uint8_t drv, uint16_t reloc)
{
	// prepare DMA load
    if(c64->client) { // we are locked by a client, likely: user interface
    	c64->client->release_host(); // disconnect from user interface
    	c64->client = 0;
	}
    if(!c64->isFrozen) {
    	c64->stop(false);
    }

    C64_POKE(C64_BOOTCRT_RUNCODE, run_code);

    C64_POKE(C64_BOOTCRT_DOSYNC, (c64->cfg->get_value(CFG_C64_DO_SYNC) == 1) ? 1 : 0);

    CbmFileName cbm;
    cbm.init(name);
    const char *p = cbm.getName();
    for(int i=0; i<16; i++) {
        C64_POKE(C64_BOOTCRT_NAME+i, p[i]);
    }
    C64_POKE(C64_BOOTCRT_NAMELEN, strlen(p));

    C64_POKE(C64_BOOTCRT_HANDSHAKE, 0x80); // initial boot cart handshake

    boot_cart.custom_addr = (void *)&_bootcrt_65_start;
    c64->start_cartridge(&boot_cart);

	// perform DMA load
    wait_ms(100);

	// handshake with boot cart
    c64->stop(false);

    C64_POKE(C64_BOOTCRT_DRIVENUM, drv);    // drive number for printout

	C64_POKE(C64_BOOTCRT_HANDSHAKE, 0x40);  // signal cart ready for DMA load

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
        if (f) {
        	load_file_dma(f, 0);
        } else {
            // If we need to jump, the first two bytes are the jump address
            if (run_code & RUNCODE_JUMP_BIT) {
            	C64_POKE(C64_BOOTCRT_JUMPADDR, buffer[0]);
            	C64_POKE(C64_BOOTCRT_JUMPADDR+1, buffer[1]);
            	load_buffer_dma(buffer+2, bufferSize-2, 0);
            } else {
            	load_buffer_dma(buffer, bufferSize, 0);
            }
        }

        C64_POKE(C64_BOOTCRT_HANDSHAKE, 0); // signal DMA load done
        C64_POKE(C64_BOOTCRT_RUNCODE, run_code);
        C64_POKE(0x00BA, drv);    // fix drive number
	}

	printf("Resuming..\n");
    c64->resume();

    restoreCart();
    return 0;
}

int C64_Subsys :: load_file_dma(File *f, uint16_t reloc)
{
	uint32_t dma_load_buffer[128];
    uint32_t transferred = 0;
    uint16_t load_address = 0;
    uint8_t  *dma_load_buffer_b;
    dma_load_buffer_b = (uint8_t *)dma_load_buffer;

    if (f) {
        f->read(&load_address, 2, &transferred);
        if(transferred != 2) {
            printf("Can't read from file..\n");
            return -1;
        }
        load_address = le2cpu(load_address); // make sure we can interpret the word correctly (endianness)
        printf("Load address: %4x...", load_address);
    }
    if(reloc) {
    	load_address = reloc;
    	printf(" -> %4x ..", load_address);
    }
    int max_length = 65536 - int(load_address); // never exceed $FFFF
    int block = 510;
    printf("Now loading...");
	volatile uint8_t *dest = (volatile uint8_t *)(C64_MEMORY_BASE + load_address);

	/* Now actually load the file */
	int total_trans = 0;
	while (max_length > 0) {
		FRESULT fres = f->read(dma_load_buffer, block, &transferred);
		if (fres != FR_OK) {
			printf("Error reading from file. %s\n", FileSystem :: get_error_string(fres));
			return -1;
		}
		//printf("[%d]", transferred);
		total_trans += transferred;
		volatile uint8_t *d = dest;
		for (int i=0;i<transferred;i++) {
			*(d++) = dma_load_buffer_b[i];
		}
		if (transferred < block) {
			break;
		}
		dest += transferred;
		max_length -= transferred;
		block = (max_length > 512) ? 512 : max_length;
	}
	uint16_t end_address = load_address + total_trans;
	printf("DMA load complete: $%4x-$%4x\n", load_address, end_address);

	// The following pokes are documented in the C64 documentation and are not
	// Ultimate specific.
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

	return total_trans;
}

int C64_Subsys :: load_buffer_dma(const uint8_t *buffer, const int bufferSize, uint16_t reloc)
{
    uint16_t load_address = 0;

    load_address = (uint16_t)buffer[0] | ((uint16_t)buffer[1]) << 8;
	printf("Load address: %4x...", load_address);

	if (reloc) {
    	load_address = reloc;
    	printf(" -> %4x ..", load_address);
    }
	volatile uint8_t *dest = (volatile uint8_t *)(C64_MEMORY_BASE + load_address);

	memcpy((void *)dest, buffer + 2, bufferSize - 2);

	uint16_t end_address = load_address + bufferSize - 2;
	printf("DMA load complete: $%4x-$%4x\n", load_address, end_address);

    // The following pokes are documented in the C64 documentation and are not
    // Ultimate specific.
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

	return bufferSize - 2;
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

void C64_Subsys :: measure_timing(const char *path)
{
    uint8_t *buffer = new uint8_t[64 * 1024];

    c64->measure_timing(buffer);

    // write file
    mstring fn(path);
    fn += "bus_measurement.bin";
    FILE *fo = fopen(fn.c_str(), "wb");
    if (fo) {
        int t = fwrite(buffer, 2048, 32, fo);
        printf("%d blocks written.\n", t);
        fclose(fo);
    } else {
        printf("Couldn't open file %s\n", fn.c_str());
    }

    delete[] buffer;
}
