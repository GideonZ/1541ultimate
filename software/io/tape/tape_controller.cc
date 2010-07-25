/*************************************************************/
/* Tape Emulator Control                                     */
/*************************************************************/
#include "tape_controller.h"
#include "menu.h"
#include "filemanager.h"
#include "spiflash.h"

TapeController *tape_controller = NULL; // globally static

#define MENU_C2N_PAUSE  0x3201
#define MENU_C2N_RESUME 0x3202
#define MENU_C2N_STATUS 0x3203

static void poll_tape(Event &e)
{
	tape_controller->poll(e);
}

TapeController :: TapeController()
{
	file = NULL;
	stop();
    poll_list.append(&poll_tape);
	main_menu_static_items.append(new MenuItemGlobal(this, "Pause Tape", MENU_C2N_PAUSE));
	main_menu_static_items.append(new MenuItemGlobal(this, "Resume Tape", MENU_C2N_RESUME));
//	main_menu_static_items.append(new MenuItemGlobal(this, "Tape status", MENU_C2N_STATUS));
}

TapeController :: ~TapeController()
{
	poll_list.remove(&poll_tape);
}
	
void TapeController :: stop()
{
	TAPE_CONTROL = C2N_CLEAR_ERROR | C2N_FLUSH_FIFO;
	TAPE_CONTROL = 0;

	if(file) {	
		printf("Closing tape file..\n");
		root.fclose(file);
	}
	file = NULL;
}
	
void TapeController :: start()
{
	printf("Start Tape.. Status = %b.\n", TAPE_STATUS);
	TAPE_CONTROL = C2N_CLEAR_ERROR | C2N_FLUSH_FIFO;
	TAPE_CONTROL = 0;
	
	for(int i=0;i<16;i++) { // preload some blocks
		if(TAPE_STATUS & C2N_STAT_FIFO_AF)
			break;
			
		read_block();
	}
	TAPE_CONTROL = C2N_ENABLE | BYTE(mode << 3);
}
	
void TapeController :: read_block()
{
	if(!file)
		return;
	
	UINT bytes_read;

	if(block > length)
		block = length;

	file->read((void *)TAPE_DATA, block, &bytes_read);

	if(bytes_read == 0) {
		stop();
	}
	printf(".");
	length -= bytes_read;
	block = 512;
}
	
void TapeController :: poll(Event &e)
{
	if(!file)
		return;

	// close 
	if(!file->node) {
		stop();
		return;
	}
	
	if(e.type == e_object_private_cmd) {
		if(e.object == this) {
			switch(e.param) {
				case MENU_C2N_PAUSE:
					TAPE_CONTROL = BYTE(mode << 3);
					break;
				case MENU_C2N_RESUME:
					TAPE_CONTROL = BYTE(mode << 3) | C2N_ENABLE;
					break;
                case MENU_C2N_STATUS:
                    printf("Tape status = %b\n", TAPE_STATUS);
//                    flash.reboot(0);
                    break;
				default:
					break;
			}
		}
	}
		
	BYTE st = TAPE_STATUS;
	if(st & C2N_STAT_ENABLED) { // we are enabled
		if(!(st & C2N_STAT_FIFO_AF)) {
			read_block();
		}
	}
}
	
void TapeController :: set_file(File *f, DWORD len, int m)
{
	file = f;
	length = len;
	block = 512 - 20;
	mode = m; 
}
		
