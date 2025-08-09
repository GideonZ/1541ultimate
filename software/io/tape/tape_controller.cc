/*************************************************************/
/* Tape Emulator Control                                     */
/*************************************************************/
#include "tape_controller.h"
#include "menu.h"
#include "filemanager.h"
#include "c64.h"
#include "init_function.h"

TapeController *tape_controller = NULL; // globally static
static void init_tape_controller(void *_a, void *_b)
{
    if(getFpgaCapabilities() & CAPAB_C2N_STREAMER)
	    tape_controller = new TapeController;
}
InitFunction tape_playback_init("Tape Playback Controller", init_tape_controller, NULL, NULL, 60);

#define MENU_C2N_PAUSE         0x3201
#define MENU_C2N_RESUME        0x3202
#define MENU_C2N_STATUS        0x3203
#define MENU_C2N_STOP          0x3204

const char *tick_rates[] = { "0.98 MHz (PAL)", "1.02 MHz (NTSC)" };

#define CFG_TAPE_RATE  1

struct t_cfg_definition tape_config[] = {
    { CFG_TAPE_RATE,    CFG_TYPE_ENUM,   "Tape Playback Rate",           "%s", tick_rates, 0,  1, 0 },
    { CFG_TYPE_END,     CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

TapeController :: TapeController() : SubSystem(SUBSYSID_TAPE_PLAYER)
{
    fm = FileManager :: getFileManager();
    register_store(0x54415045, "Tape Settings", tape_config);
    file = NULL;
	paused = 0;
	recording = 0;
	controlByte = 0;
	blockBuffer = new uint8_t[512];
	stop();
	taskHandle = 0;
	if (getFpgaCapabilities() & CAPAB_C2N_STREAMER) {
		xTaskCreate( TapeController :: poll_static, "TapePlayer", configMINIMAL_STACK_SIZE, this, PRIO_REALTIME, &taskHandle );
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
		tc->lock("tape poll");
		tc->poll();
		tc->unlock();
		if (PLAYBACK_STATUS & C2N_STAT_ENABLED) {
			vTaskDelay(5);
		} else {
			vTaskDelay(250);
		}
	}
}

void TapeController :: create_task_items(void)
{
    TaskCategory *cat = TasksCollection :: getCategory("Tape", SORT_ORDER_TAPE);
    myActions.pause = new Action("Pause Tape Playback", SUBSYSID_TAPE_PLAYER, MENU_C2N_PAUSE);
    myActions.resume = new Action("Resume Tape Playback", SUBSYSID_TAPE_PLAYER, MENU_C2N_RESUME);
    myActions.stop = new Action("Stop Tape Playback", SUBSYSID_TAPE_PLAYER, MENU_C2N_STOP);
    // myActions.rewind = new Action("Rewind Tape", SUBSYSID_TAPE_PLAYER, MENU_C2N_REWIND);

    cat->append(myActions.pause);
    cat->append(myActions.resume);
    cat->append(myActions.stop);
    //cat->append(myActions.rewind);

    myActions.pause->hide();
    myActions.resume->hide();
    myActions.stop->hide();
}

void TapeController :: update_task_items(bool writablePath, Path *path)
{
    if(!file)
        return;
	if(!file->isValid()) {
    	close();
		return;
	}
	if(paused) {
	    myActions.pause->hide();
	    myActions.resume->show();
	    myActions.stop->show();
	} else {
        myActions.pause->show();
        myActions.resume->hide();
        myActions.stop->hide();
	}
}

void TapeController :: stop()
{
	PLAYBACK_CONTROL = C2N_CLEAR_ERROR | C2N_FLUSH_FIFO;
	PLAYBACK_CONTROL = 0; // also clears sense pin and disables output
}

void TapeController :: close()
{
	if(file) {
		printf("Closing tape file..\n");
        fm->fclose(file);
	}
	file = NULL;
}
	
void TapeController :: start(int playout_pin) // pin = 1: read, pin = 2: write
{
	stop();

	printf("Start Tape.. Status = %b. [", PLAYBACK_STATUS);
	PLAYBACK_CONTROL = C2N_CLEAR_ERROR | C2N_FLUSH_FIFO;
	PLAYBACK_CONTROL = 0;
	paused = 0;
	
    // insert 2.5 (was one) second pause to start with
    *PLAYBACK_DATA = 0x00;
    *PLAYBACK_DATA = 0x95; //0xA2;
    *PLAYBACK_DATA = 0x95; //0x08;
    *PLAYBACK_DATA = 0x25; //0x0F; 25

	controlByte = C2N_ENABLE | uint8_t(mode << 3) | uint8_t(playout_pin << 6);
	if (playout_pin == 1) { // normal playback: WE control the Sense PIN
		controlByte |= C2N_SENSE;
	}
	if (cfg->get_value(CFG_TAPE_RATE)) {
	    controlByte |= C2N_RATE;
	}

	// preload some blocks
	for(int i=0;i<16;i++) {
		if(PLAYBACK_STATUS & C2N_STAT_FIFO_AF)
			break;

		read_block();
	}

	PLAYBACK_CONTROL = controlByte;
    recording = (playout_pin == 2);
	printf("%b] Status = %b.\n", controlByte, PLAYBACK_STATUS);
	state = 0;
}
	
void TapeController :: read_block()
{
	if(!file) {
		state = 1;
		return;
	}
	if(!file->isValid()) {
		state = 1;
    	close();
        return;
    }

	uint32_t bytes_read;

	if(block > length)
		block = length;

	if(block <= 0) {
		state = 1;
		return;
	}	

	file->read(blockBuffer, block, &bytes_read);
	for(int i=0;i<bytes_read;i++)// not sure if memcpy copies the bytes in the right order.
		*PLAYBACK_DATA = blockBuffer[i];

	if(bytes_read != block) {
		printf("[%d of %d]", bytes_read, block);
	}

	printf(".");
	length -= block;
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
			switch(state) {
			case 0:
				read_block();
				break;
			case 1:
				*PLAYBACK_DATA = 123;
				state = 2;
				break;
			case 2:
				if (PLAYBACK_STATUS & C2N_STAT_FIFO_EMPTY) {
					state = 3;
					close();
	                stop();
			        if (recording) {
			        	C64::getMachine()->setButtonPushed();
			        }
			    }
				break;
			default:
				break;
			}
		}
	}
}
	
SubsysResultCode_e TapeController :: executeCommand(SubsysCommand *cmd)
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
	return SSRET_OK;
}

void TapeController :: set_file(File *f, uint32_t len, int m, int offset)
{
	close();
	file = f;
	length = f->get_size() - offset;
	mode = m;

	if (offset < 20) {
	    offset = 20;
	}
	file->seek((uint32_t)offset);
	block = 512 - (offset & 0x1FF);
}

