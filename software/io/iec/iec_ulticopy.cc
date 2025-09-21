#include "iec_ulticopy.h"
#include "init_function.h"
#include "c64.h"
#include "endianness.h"

#define MENU_IEC_WARP_8      0xCA13
#define MENU_IEC_WARP_9      0xCA14
#define MENU_IEC_WARP_10     0xCA15
#define MENU_IEC_WARP_11     0xCA16

//extern BYTE _warp_rom_65_start;
extern uint8_t _ulticopy_65_start;
extern uint32_t _ulticopy_65_size;

static UltiCopy *uc = NULL;
void create_ulticopy(void *_a, void *_b)
{
    if(!(getFpgaCapabilities() & CAPAB_HARDWARE_IEC))
        return;
    uc = new UltiCopy();
}
InitFunction ulticopy_init("UltiCopy", create_ulticopy, NULL, NULL, 13);

UltiCopy :: UltiCopy() : SubSystem(SUBSYSID_ULTICOPY)
{
	fm = FileManager :: getFileManager();
    intf = IecInterface :: get_iec_interface();

    path = fm->get_new_path("UltiCopy Path");
    cmd_ui = 0;
	ui_window = NULL;

    ulticopy_bin_image = new BinImage("UltiCopy", 35);
    ulticopyBusy = xSemaphoreCreateBinary();
    ulticopyMutex = xSemaphoreCreateMutex();
}

UltiCopy :: ~UltiCopy()
{
    fm->release_path(path);
}

void UltiCopy :: create_task_items(void)
{
    TaskCategory *uc = TasksCollection :: getCategory("UltiCopy", SORT_ORDER_SOFTIEC+1);
    myActions.ulticopy8  = new Action("Drive 8",  SUBSYSID_ULTICOPY, MENU_IEC_WARP_8);
    myActions.ulticopy9  = new Action("Drive 9",  SUBSYSID_ULTICOPY, MENU_IEC_WARP_9);
    myActions.ulticopy10 = new Action("Drive 10", SUBSYSID_ULTICOPY, MENU_IEC_WARP_10);
    myActions.ulticopy11 = new Action("Drive 11", SUBSYSID_ULTICOPY, MENU_IEC_WARP_11);

    uc->append(myActions.ulticopy8);
    uc->append(myActions.ulticopy9);
    uc->append(myActions.ulticopy10);
    uc->append(myActions.ulticopy11);
}

// called from GUI task
void UltiCopy :: update_task_items(bool writablePath, Path *path)
{
	if (writablePath) {
		myActions.ulticopy8->enable();
		myActions.ulticopy9->enable();
		myActions.ulticopy10->enable();
		myActions.ulticopy11->enable();
	} else {
		myActions.ulticopy8->disable();
		myActions.ulticopy9->disable();
		myActions.ulticopy10->disable();
		myActions.ulticopy11->disable();
	}
}

// this is actually the ulticopy loop, executed from the IEC task
void UltiCopy :: S_warp_loop(IecSlave *sl, void *data)
{
    printf("S_warp_loop!\n");
    UltiCopy *uc = (UltiCopy *)data;
    if (uc->start_warp_iec()) {
        uc->warp_loop();
        uc->get_warp_error();
    }
    // The loop now exits, and returns to normal IEC polling
    xSemaphoreGive(uc->ulticopyBusy);
}

void UltiCopy :: warp_loop()
{
    uint8_t data;
    bool done = false;
    wait_irq = false;
    int cnt = 0;
    printf("Warp loop..\n");
    while(!done) {

        if(wait_irq) {
            if (HW_IEC_IRQ_R & HW_IEC_IRQ_BIT) {
                get_warp_data();
            }
            vTaskDelay(1);
            continue;
        }

        uint8_t a = HW_IEC_RX_FIFO_STATUS;
        if (!(a & IEC_FIFO_EMPTY)) {
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
                        done = true;
                        break;

                    case WARP_ACK:
                        DBGIEC(".WARP:ACK.");
                        break;

                    default:
                        DBGIECV(".<%b>.", data);
                        break;
                }
            } else {
                printf("Unexpected data: %b", data);
            }
        }
    }
}

// called from GUI task
SubsysResultCode_e UltiCopy :: executeCommand(SubsysCommand *cmd)
{
    path->cd(cmd->path.c_str());
    cmd_ui = cmd->user_interface;

    switch (cmd->functionID) {
    case MENU_IEC_WARP_8:
        start_warp(8);
        break;
    case MENU_IEC_WARP_9:
        start_warp(9);
        break;
    case MENU_IEC_WARP_10:
        start_warp(10);
        break;
    case MENU_IEC_WARP_11:
        start_warp(11);
        break;
    default:
        break;
    }
    return SSRET_OK;
}


#include "c1541.h"
extern C1541 *c1541_A;
extern C1541 *c1541_B;

// called from GUI task
void UltiCopy :: start_warp(int drive)
{
	// First try to obtain a lock on this function
	if (xSemaphoreTake(ulticopyMutex, 0) == pdFALSE) {
		cmd_ui->popup("Ulticopy is already in use", BUTTON_OK);
		return;
	}

	// FIXME: Make sure that the C64 is actually frozen!
    printf("Starting IEC Warp Mode.\n");
    C64_POKE(0xDD00,0x07); // make sure the C64 that might be connected does not interfere
    // make sure that our local drives are turned off as well
    if (c1541_A) {
        c1541_A->drive_power(false);
    }
    if (c1541_B) {
        c1541_B->drive_power(false);
    }

    ui_window = new UltiCopyWindow(cmd_ui);
    ui_window->init();
    cmd_ui->activate_uiobject(ui_window); // now we have focus
    ui_window->window->move_cursor(15,10);
    ui_window->window->output("Loading...");

    warp_drive = drive;

    // IEC Processor should now run our UltiCopy code instead of the regular loop
    // While this is ongoing, the IEC thread directly handles the GUI. This is
    // dirty, but it is also easy.
ulticopy_again:
    // Clear wrongly pending semaphore ready flags
    xSemaphoreTake(ulticopyBusy, 0);
    iec_closure_t cb = { NULL, S_warp_loop, this };
    intf->run_from_iec(&cb);

    // The IEC thread is now doing its thing. Once a copy has completed,
    // it gives the semaphore again. That's when our thread continues.
    while(1) {
		printf("GUI Task waiting for the IEC task to be ready.\n");
		xSemaphoreTake(ulticopyBusy, portMAX_DELAY);

		// Ulticopy is done
		printf("UltiCopy warp mode returned.\n");

		if(warp_return_code & 0x80) {
			last_track++;
			printf("Error on track %d.\n", last_track);
		} else if(warp_return_code == 0) {
			save_copied_disk();
			if(cmd_ui->popup("Another disk?", BUTTON_YES|BUTTON_NO) == BUTTON_YES) {
				ui_window->window->clear();
                goto ulticopy_again;
			} else {
				break;
			}
		} else if(warp_return_code < 0x40) {
			cmd_ui->popup("Error reading disk..", BUTTON_OK);
			break;
		}
	}

ulticopy_close:
    ui_window->close();
	if (c1541_A) {
		c1541_A->effectuate_settings();
	}
	if (c1541_B) {
		c1541_B->effectuate_settings();
	}
    intf->configure();
    xSemaphoreGive(ulticopyMutex);
}

// This is called from the IEC thread.
bool UltiCopy :: start_warp_iec(void)
{
    // Force processor to run
    HW_IEC_RESET_ENABLE = 1;
    
    if(!intf->run_drive_code(warp_drive, 0x400, &_ulticopy_65_start, (int)&_ulticopy_65_size)) {
        cmd_ui->popup("Error accessing drive..", BUTTON_OK);
        ui_window->close();
        warp_return_code = 0x1F;
        return false;
    }

    printf("Drive code loaded.\n");

    ui_window->window->clear();
    // clear pending interrupt if any
    HW_IEC_IRQ = 0;
    // push warp command into down-fifo; Go!
	HW_IEC_TX_FIFO_RELEASE = 1;
    HW_IEC_TX_CTRL = IEC_CMD_GO_WARP;
    last_track = 0;
    return true;
}

// Subroutine, called from eh IEC poll loop when IRQ is set
void UltiCopy :: get_warp_data(void)
{
    uint32_t read;
    uint32_t temp[65];
    uint32_t *dw = &temp[0];

    int err = 0;
    uint16_t fifo_count = uint16_t(HW_IEC_UP_FIFO_COUNT_LO) | (uint16_t(HW_IEC_UP_FIFO_COUNT_HI) << 8);

    for(int i=0;i<64;i++) {
    	GCR_DECODER_GCR_IN_32 = HW_IEC_RX_DATA_32; // first in first out, endianness OK
        GCR_DECODER_GCR_IN = HW_IEC_RX_DATA;
        *(dw++) = GCR_DECODER_BIN_OUT_32;
        if(GCR_DECODER_ERRORS)
            err++;
    }
    read = HW_IEC_RX_DATA_32;
    GCR_DECODER_GCR_IN_32 = read;
    GCR_DECODER_GCR_IN = 0x55;
    *(dw++) = GCR_DECODER_BIN_OUT_32;

    read = cpu_to_32le(read);
    uint8_t sector = (uint8_t)(read >> 24);

    uint8_t track = HW_IEC_RX_DATA;
    uint8_t *dest = ulticopy_bin_image->get_sector_pointer(track, sector);
    uint8_t *src = (uint8_t *)temp;
    // printf("Sector {%b %b (%p -> %p}\n", track, sector, src, dest);
    if (dest) {
		ui_window->window->set_char(track-1,sector+1,err?'-':'*');
		ui_window->parent_win->sync();
		last_track = track;
		if(dest) {
			for(int i=0;i<256;i++) {
				*(dest++) = *(++src); // asymmetric: We copy from 1.. to 0..., so we increment src first
			}
		}
    }
    // clear pending interrupt
    wait_irq = false;
    HW_IEC_IRQ = 0;
}

// called from IEC context
void UltiCopy :: get_warp_error(void)
{
    while (HW_IEC_RX_FIFO_STATUS & IEC_FIFO_EMPTY)
        vTaskDelay(2);
        
    warp_return_code = HW_IEC_RX_DATA;
    printf("{Warp Error: %b}", warp_return_code);
    // clear pending interrupt
    HW_IEC_IRQ = 0;
}

void UltiCopy :: save_copied_disk()
{
    static char buffer[40];
    int save_result;
    File *f = 0;
    int res;
    BinImage *bin;
    
    ulticopy_bin_image->num_tracks = 35; // standard!

	// buffer[0] = 0;
    if (cmd_ui->cfg->get_value(CFG_USERIF_ULTICOPY_NAME)) {
        ulticopy_bin_image->get_sensible_name(buffer);
    }
	res = cmd_ui->string_box("Give name for copied disk..", buffer, 22);
	if(res > 0) {
		fix_filename(buffer);
		set_extension(buffer, ".d64", 32);
        FRESULT fres = fm->fopen(path, buffer, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
		if(f) {
            cmd_ui->show_progress("Saving D64..", 100);
            save_result = ulticopy_bin_image->save(f, cmd_ui);
            cmd_ui->hide_progress();
    		printf("Result of save: %d.\n", save_result);
            fm->fclose(f);
		} else {
			printf("Can't create file '%s': %s\n", buffer, FileSystem::get_error_string(fres));
			cmd_ui->popup("Can't create file.", BUTTON_OK);
		}
	}
}
              
/*********************************************************************/
/* Copier user interface screen
/*********************************************************************/
UltiCopyWindow :: UltiCopyWindow(UserInterface *ui) : UIObject(ui)
{
    parent_win = NULL;
    keyb = NULL;
    window = NULL;
    return_code = 0;
}
    
UltiCopyWindow :: ~UltiCopyWindow()
{
}    

void UltiCopyWindow :: init()
{
    parent_win = get_ui()->get_screen();
    keyb = get_ui()->get_keyboard();
    window = new Window(parent_win, 0, 1, 40, 23);
    window->draw_border_horiz();
    window->clear();
}
    
void UltiCopyWindow :: deinit(void)
{
    if (window)
        delete window;
}

int UltiCopyWindow :: handle_key(uint8_t c)
{
	return 0;
}

int UltiCopyWindow :: poll(int a)
{
    return return_code;
}
        
void UltiCopyWindow :: close(void)
{
    return_code = 1;
}
   


