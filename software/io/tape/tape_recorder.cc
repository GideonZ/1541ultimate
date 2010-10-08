/*************************************************************/
/* Tape Recorder Control                                     */
/*************************************************************/
#include "tape_recorder.h"
#include "menu.h"
#include "filemanager.h"

TapeRecorder *tape_recorder = NULL; // globally static

#define MENU_REC_SAMPLE_TAPE   0x3211
#define MENU_REC_RECORD_TO_TAP 0x3212

static void poll_tape(Event &e)
{
	tape_recorder->poll(e);
}

TapeRecorder :: TapeRecorder()
{
	file = NULL;
	stop();
    poll_list.append(&poll_tape);
	main_menu_static_items.append(new MenuItemGlobal(this, "Sample Tape to TAP", MENU_REC_SAMPLE_TAPE));
	main_menu_static_items.append(new MenuItemGlobal(this, "Record C64 to TAP", MENU_REC_RECORD_TO_TAP));
}

TapeRecorder :: ~TapeRecorder()
{
	poll_list.remove(&poll_tape);
}
	
void TapeRecorder :: stop()
{
	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
	RECORD_CONTROL = 0;

	if(file) {	
		printf("Closing tape file..\n");
		root.fclose(file);
	}
	file = NULL;
}
	
void TapeRecorder :: start()
{
	printf("Start Tape.. Status = %b.\n", RECORD_STATUS);
	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
	RECORD_CONTROL = 0;
	
	for(int i=0;i<16;i++) { // preload some blocks
		if(RECORD_STATUS & REC_STAT_FIFO_AF)
			break;
			
		read_block();
	}
	RECORD_CONTROL = REC_ENABLE | BYTE(mode << 3);
}
	
void TapeRecorder :: write_block()
{
	if(!file)
		return;
	
	UINT bytes_read;

	if(block > length)
		block = length;

	file->write((void *)RECORD_DATA, block, &bytes_read);

	if(bytes_read == 0) {
		stop();
	}
	printf(".");
	length -= bytes_read;
	block = 512;
}
	
void TapeRecorder :: poll(Event &e)
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
				case MENU_REC_SAMPLE_TAPE:
					RECORD_CONTROL = (mode)?REC_MODE_SELECT:0;
					break;
				case MENU_REC_RECORD_TO_TAP:
					RECORD_CONTROL = ((mode)?REC_MODE_SELECT:0) | REC_ENABLE;
					break;
                case MENU_REC_STATUS:
                    printf("Tape status = %b\n", RECORD_STATUS);
                    break;
				default:
					break;
			}
		}
	}
		
	BYTE st = RECORD_STATUS;
	if(st & REC_STAT_ENABLED) { // we are enabled
		if(!(st & REC_STAT_FIFO_AF)) {
			read_block();
		}
	}
}
	
void TapeRecorder :: set_file(File *f, DWORD len, int m)
{
	file = f;
	length = len;
	block = 512 - 20;
	mode = m; 
}
