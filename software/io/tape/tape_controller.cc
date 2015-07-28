/*************************************************************/
/* Tape Emulator Control                                     */
/*************************************************************/
#include "tape_controller.h"
#include "menu.h"
#include "filemanager.h"
#include "c64.h"

TapeController *tape_controller = NULL; // globally static

#define MENU_C2N_PAUSE         0x3201
#define MENU_C2N_RESUME        0x3202
#define MENU_C2N_STATUS        0x3203
#define MENU_C2N_STOP          0x3204

TapeController :: TapeController() : SubSystem(SUBSYSID_TAPE_PLAYER)
{
    fm = FileManager :: getFileManager();
	file = NULL;
	paused = 0;
	recording = 0;
	controlByte = 0;
	blockBuffer = new uint8_t[512];
	stop();
	taskHandle = 0;
	if (getFpgaCapabilities() & CAPAB_C2N_STREAMER) {
		xTaskCreate( TapeController :: poll_static, "TapePlayer", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 3, &taskHandle );
	}
}

TapeController :: ~TapeController()
{
	if (taskHandle) {
		vTaskDelete(taskHandle);
	}
	delete blockBuffer;
}

void TapeController :: poll_static(void *a)
{
	TapeController *tc = (TapeController *)a;
	while(1) {
		tc->poll();
		if (PLAYBACK_STATUS & C2N_STAT_ENABLED) {
			vTaskDelay(5);
		} else {
			vTaskDelay(250);
		}
	}
}

int  TapeController :: fetch_task_items(Path *path, IndexedList<Action*> &item_list)
{
    if(!file)
        return 0;
	if(!file->isValid()) {
    	close();
		return 0;
	}
	if(paused)
		item_list.append(new Action("Resume Tape Playback", SUBSYSID_TAPE_PLAYER, MENU_C2N_RESUME));
	else
		item_list.append(new Action("Pause Tape Playback", SUBSYSID_TAPE_PLAYER, MENU_C2N_PAUSE));
    item_list.append(new Action("Stop Tape Playback", SUBSYSID_TAPE_PLAYER, MENU_C2N_STOP));
	return 2;
}

void TapeController :: stop()
{
	PLAYBACK_CONTROL = C2N_CLEAR_ERROR | C2N_FLUSH_FIFO;
	PLAYBACK_CONTROL = 0; // also clears sense pin
}

void TapeController :: close()
{
	if(file) {
		printf("Closing tape file..\n");
        fm->fclose(file);
	}
	file = NULL;
}
	
void TapeController :: start(int playout_pin)
{
	stop();
	printf("Start Tape.. Status = %b. [", PLAYBACK_STATUS);
	PLAYBACK_CONTROL = C2N_CLEAR_ERROR | C2N_FLUSH_FIFO;
	PLAYBACK_CONTROL = 0;
	paused = 0;
	
	for(int i=0;i<16;i++) { // preload some blocks
		if(PLAYBACK_STATUS & C2N_STAT_FIFO_AF)
			break;
			
		read_block();
	}
	controlByte = C2N_SENSE | C2N_ENABLE | uint8_t(mode << 3) | uint8_t(playout_pin << 6);
	PLAYBACK_CONTROL = controlByte;
    recording = playout_pin;
	printf("] Status = %b.\n", PLAYBACK_STATUS);
}
	
void TapeController :: read_block()
{
	if(!file)
		return;
	if(!file->isValid()) {
    	close();
        return;
    }
	
	uint32_t bytes_read;

	if(block > length)
		block = length;

	if(!block) {
        if (PLAYBACK_STATUS & C2N_STAT_FIFO_EMPTY) {
        	vTaskDelay(100);
            close();
            if (recording) {
            	c64->setButtonPushed();
            }
        }
		return;
	}	
	file->read(blockBuffer, block, &bytes_read);
	if(bytes_read != block) {
		printf("[%d of %d]", bytes_read, block);
	}
	for(int i=0;i<bytes_read;i++)// not sure if memcpy copies the bytes in the right order.
		*PLAYBACK_DATA = blockBuffer[i];

	printf(".");
	length -= bytes_read;
	block = 512;
}
	
void TapeController :: poll()
{
	if(!file)
		return;

	if(!file->isValid()) {
    	close();
        return;
    }
	
	uint8_t st = PLAYBACK_STATUS;
	if(st & C2N_STAT_ENABLED) { // we are enabled
		if(!(st & C2N_STAT_FIFO_AF)) {
			read_block();
		}
	}
}
	
int TapeController :: executeCommand(SubsysCommand *cmd)
{
	switch(cmd->functionID) {
		case MENU_C2N_PAUSE:
			PLAYBACK_CONTROL = (controlByte & ~C2N_ENABLE);
			paused = 1;
			break;
		case MENU_C2N_RESUME:
			PLAYBACK_CONTROL = controlByte;
			paused = 0;
			break;
		case MENU_C2N_STATUS:
			printf("Tape status = %b\n", PLAYBACK_STATUS);
			break;
		case MENU_C2N_STOP:
			close();
			stop();
			break;
		default:
			break;
	}
	return 0;
}

void TapeController :: set_file(File *f, uint32_t len, int m)
{
	close();
	file = f;
	length = len;
	block = 512 - 20;
	mode = m; 
}
		
