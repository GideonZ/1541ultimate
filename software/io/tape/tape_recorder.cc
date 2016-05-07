/*************************************************************/
/* Tape Recorder Control                                     */
/*************************************************************/
#include "tape_recorder.h"
#include "userinterface.h"
#include "filemanager.h"

extern "C" {
    #include "dump_hex.h"
}

TapeRecorder *tape_recorder = NULL; // globally static


extern "C" BaseType_t tape_recorder_irq(void)
{
    ioWrite8(UART_DATA, 0x2D);
    if(tape_recorder)
    	tape_recorder->irq();
    return pdFALSE; // it will get polled, no immediate action
}

__inline uint32_t cpu_to_32le(uint32_t a)
{
#ifdef NIOS
	return a;
#else
	uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
#endif
}

TapeRecorder :: TapeRecorder() : SubSystem(SUBSYSID_TAPE_RECORDER)
{
    fm = FileManager :: getFileManager();
    recording = 0;
    select = 0;
	file = NULL;
	last_user_interface = 0;
	stop(REC_ERR_OK);
	taskHandle = 0;

	cache = new uint8_t[REC_CACHE_SIZE];
    for(int i=0;i<REC_NUM_CACHE_BLOCKS;i++) {
        cache_blocks[i] = (uint32_t *)&cache[512*i];
    }
	if (getFpgaCapabilities() & CAPAB_C2N_RECORDER) {
		xTaskCreate( TapeRecorder :: poll_tape_rec, "TapeRecorder", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 3, &taskHandle );
        ioWrite8(ITU_IRQ_ENABLE, 0x08);
	}
}

TapeRecorder :: ~TapeRecorder()
{
    ioWrite8(ITU_IRQ_DISABLE, 0x08);
	stop(REC_ERR_OK);
    delete[] cache;
}
	
int TapeRecorder :: executeCommand(SubsysCommand *cmd)
{
	if (cmd->user_interface) {
		last_user_interface = cmd->user_interface;
	}
	UserInterface *ui = (UserInterface *)last_user_interface;

	switch(cmd->functionID) {
		case MENU_REC_FINISH:
			flush();
			if (ui) {
				if (!ui->is_available()) {
					ui->appear();
					vTaskDelay(30);
				}
				if (ui->is_available()) {
					ui->popup("Tape capture stopped.", BUTTON_OK);
				} else {
					printf("Damn user interface is not available!\n");
				}
			}
			break;
		case MENU_REC_SAMPLE_TAPE:
			select = 0;
			if(request_file(cmd))
				start();
			break;
		case MENU_REC_RECORD_TO_TAP:
			select = 1;
			if(request_file(cmd))
				start();
			break;
		default:
			break;
	}
	return 0;
}

int  TapeRecorder :: fetch_task_items(Path *path, IndexedList<Action*> &item_list)
{
	int items = 0;
	if(recording) {
		item_list.append(new Action("Finish Rec. to TAP", SUBSYSID_TAPE_RECORDER, MENU_REC_FINISH));
		items = 1;
	}
    else {
    	if (fm->is_path_writable(path)) {
			item_list.append(new Action("Sample tape to TAP", SUBSYSID_TAPE_RECORDER, MENU_REC_SAMPLE_TAPE));
			item_list.append(new Action("Capture save to TAP", SUBSYSID_TAPE_RECORDER, MENU_REC_RECORD_TO_TAP));
			items = 2;
    	}
	}
	return items;
}

void TapeRecorder :: stop(int error)
{
    error_code = error;
    
	RECORD_CONTROL = 0;
    recording = 0;
    
    if (error != REC_ERR_OK) {
    	// FIXME: freeze
    }
	if(file) {	
        printf("Flush TAP..\n");
        flush();
		printf("Closing tape file..\n");
		fm->fclose((File *)file);
	} else {
	    printf("Stopping. File = NULL. ERR = %d\n", error);
    }
	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
	file = NULL;
}
	
void TapeRecorder :: start()
{
	printf("Start recording.. Status = %b.\n", RECORD_STATUS);
	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
	RECORD_CONTROL = REC_MODE_FALLING;
	
	ENTER_SAFE_SECTION;

	if(select)
	    RECORD_CONTROL = REC_ENABLE | REC_MODE_RISING | REC_SELECT_WRITE | REC_IRQ_EN;
	else
	    RECORD_CONTROL = REC_ENABLE | REC_MODE_FALLING | REC_SELECT_READ | REC_IRQ_EN;

	recording = 1;
    block_in = 0;
    block_out = 0;
    blocks_cached = 0;
    blocks_written = 0;

	LEAVE_SAFE_SECTION;
	
}

void TapeRecorder :: irq()
{
    if (!recording) {
        RECORD_CONTROL = 0;
        return;
    }
            
	if(RECORD_STATUS & REC_STAT_BLOCK_AV)
		cache_block();
	else
	    printf("?");
    ioWrite8(UART_DATA, 0x2B);
}
	
void TapeRecorder :: cache_block()
{
    uint32_t *block = cache_blocks[block_in];
    for(int i=0;i<128;i++) {
        *(block++) = RECORD_DATA32;
    }
    block_in++;
    if(block_in == REC_NUM_CACHE_BLOCKS) {
        block_in = 0;
    }        
    blocks_cached++;
}

int TapeRecorder :: write_block()
{
	if(!file) {
        return REC_ERR_NO_FILE;
    }
	
	uint32_t bytes_written;

    uint32_t *block = cache_blocks[block_out];
	FRESULT res = file->write((void *)block, 512, &bytes_written);
    total_length += 512;
	if(res != FR_OK) {
		return REC_ERR_WRITE_ERROR;
	}
	printf("$");
    block_out++;
    if(block_out == REC_NUM_CACHE_BLOCKS) {
        block_out = 0;
    }        
    blocks_written++;

    return REC_ERR_OK;
}

void TapeRecorder :: flush()
{
	if(!file) {
	    error_code = REC_ERR_NO_FILE;
    	recording = 0;
    	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
    	RECORD_CONTROL = 0; // make sure the interrupt has been cleared too.
        return;
    }

    while (blocks_cached > blocks_written) {
        error_code = write_block();
    }

    uint32_t bytes_written;

    while (RECORD_STATUS & REC_STAT_BYTE_AV) {
		uint8_t *block = (uint8_t *)cache_blocks[block_in];
		int i;
		for(i=0;i<512;i++) {
			if (RECORD_STATUS & REC_STAT_BYTE_AV)
				*(block++) = RECORD_DATA;
			else
				break;
		}
		printf("Writing out remaining %d bytes.\n", i);
		FRESULT res = file->write((void *)cache_blocks[block_in], i, &bytes_written);
		total_length += bytes_written;
    }
    file->seek(16);
    uint32_t le_size = cpu_to_32le(total_length);
    file->write(&le_size, 4, &bytes_written);

	fm->fclose(file);
	file = NULL;

	recording = 0;
	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
	RECORD_CONTROL = 0; // make sure the interrupt has been cleared too.
}
	
void TapeRecorder :: poll_tape_rec(void *a)
{
	TapeRecorder *tr = (TapeRecorder *)a;
	tr->poll();
}

#include "profiler.h"
void TapeRecorder :: poll()
{
	volatile void *aap = this;

	printf("** tape recorder this = %p\n", this);
	while(1) {
		if(error_code != REC_ERR_OK) {
			PROFILER_SUB = 15;
			if (last_user_interface) {
				printf("** tape recorder this = %p\n", this);
				printf("Last User Interface = %p\n");
				UserInterface *ui = (UserInterface *)last_user_interface;
				if (ui->is_available()) {
					ui->popup("Error during tape capture.", BUTTON_OK);
					error_code = REC_ERR_OK;
				}
			}
		} /*else if ((recording >0) && (e.type == e_enter_menu)) {
			if (last_user_interface) {
				if(last_user_interface->popup("End tape capture?", BUTTON_YES | BUTTON_NO) == BUTTON_YES)
					flush();
			}
		}   */

		if (file) { // check for invalidation
			if(!file->isValid()) {
				printf("TapeRecorder: Our file got killed...\n");
				file = NULL;
				stop(REC_ERR_NO_FILE);
			}
		}
		if (recording > 0) {
			if(blocks_cached >= (blocks_written+REC_NUM_CACHE_BLOCKS)) {
				stop(REC_ERR_OVERFLOW);
			}
			while (blocks_cached > blocks_written) {
				error_code = write_block();
			}
		}
		vTaskDelay(10);
	}
}

bool TapeRecorder :: request_file(SubsysCommand *cmd)
{
    char buffer[40];
    
    if(file) {
        printf("** WARNING.. File variable is already set!!\n");
        recording = 1; // fix so we can stop and close
        return false;
    }
        
    uint32_t dummy;
    char *signature = "C64-TAPE-RAW\001\0\0\0\0\0\0\0";

	int res = cmd->user_interface->string_box("Give name for tap file..", buffer, 22);
	if(res > 0) {
        total_length = 0;
		set_extension(buffer, ".tap", 32);
		fix_filename(buffer);
        FRESULT fres = fm->fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &file);
        if(fres == FR_OK) {
            FRESULT res = file->write(signature, 20, &dummy);
            if(res != FR_OK) {
                cmd->user_interface->popup("Error writing signature", BUTTON_OK);
                return false;
            }
            return true;
        } else {
            cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
        }            
	}
	return false;
 }
