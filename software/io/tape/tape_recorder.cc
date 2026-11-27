/*************************************************************/
/* Tape Recorder Control                                     */
/*************************************************************/
#include "tape_recorder.h"
#include "userinterface.h"
#include "filemanager.h"
#include "c64.h"
#include "endianness.h"
#include "init_function.h"

extern "C" {
    #include "dump_hex.h"
}

TapeRecorder *tape_recorder = NULL; // globally static
static void init_tape_recorder(void *_a, void *_b)
{
    if(getFpgaCapabilities() & CAPAB_C2N_RECORDER)
	    tape_recorder   = new TapeRecorder;
}
InitFunction tape_record_init("Tape Recording Controller", init_tape_recorder, NULL, NULL, 61);

extern "C" BaseType_t tape_recorder_irq(void)
{
    ioWrite8(UART_DATA, 0x2D);
    if(tape_recorder)
    	tape_recorder->irq();
    return pdFALSE; // it will get polled, no immediate action
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
		xTaskCreate( TapeRecorder :: poll_tape_rec, "TapeRecorder", configMINIMAL_STACK_SIZE, this, PRIO_REALTIME, &taskHandle );
        ioWrite8(ITU_IRQ_ENABLE, ITU_INTERRUPT_TAPE);
	}
}

TapeRecorder :: ~TapeRecorder()
{
    ioWrite8(ITU_IRQ_DISABLE, ITU_INTERRUPT_TAPE);
	stop(REC_ERR_OK);
    delete[] cache;
}
	
SubsysResultCode_e TapeRecorder :: executeCommand(SubsysCommand *cmd)
{
	if (cmd->user_interface) {
		last_user_interface = cmd->user_interface;
	}
	UserInterface *ui = (UserInterface *)last_user_interface;
	SubsysCommand *c64_command;

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
			if(request_file(cmd)) {
				if (ui->is_available()) {
					ui->popup("Start tape when back in C64 screen", BUTTON_OK);
				} else {
					printf("Damn user interface is not available!\n");
				}
				c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64,
	            		C64_UNFREEZE, 0, "", "");
	            c64_command->execute();
				start();
			}
			break;
		case MENU_REC_RECORD_TO_TAP:
			select = 1;
			if(request_file(cmd))
				start();
			break;
		default:
			break;
	}
	return SSRET_OK;
}

void TapeRecorder :: create_task_items(void)
{
    TaskCategory *cat = TasksCollection :: getCategory("Tape", SORT_ORDER_TAPE);
    myActions.sample  = new Action("Sample tape to TAP", SUBSYSID_TAPE_RECORDER, MENU_REC_SAMPLE_TAPE);
    myActions.capture = new Action("Capture save to TAP", SUBSYSID_TAPE_RECORDER, MENU_REC_RECORD_TO_TAP);
    myActions.finish  = new Action("Finish Rec. to TAP", SUBSYSID_TAPE_RECORDER, MENU_REC_FINISH);

    cat->append(myActions.sample);
    cat->append(myActions.capture);
    cat->append(myActions.finish);
}

void TapeRecorder :: update_task_items(bool writablePath)
{
    if (recording) {
        myActions.finish->show();
        myActions.sample->hide();
        myActions.capture->hide();
    } else {
        myActions.finish->hide();
        myActions.sample->show();
        myActions.capture->show();
    }
    if (writablePath) {
        myActions.sample->enable();
        myActions.capture->enable();
    } else {
        myActions.sample->disable();
        myActions.capture->disable();
    }
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
        // end_capture() writes out what is left, as far as that makes sense after this error, and
        // closes the file. It is the only place that does.
        printf("Flush TAP..\n");
        end_capture((error == REC_ERR_OK) || (error == REC_ERR_OVERFLOW));
	} else {
	    printf("Stopping. File = NULL. ERR = %d\n", error);
    }
	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
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
	
	uint32_t bytes_written = 0;

    int out = block_out; // the block that is written is the one that is counted as written
    uint32_t *block = cache_blocks[out];
	FRESULT res = file->write((void *)block, 512, &bytes_written);
    total_length += bytes_written;
	if((res != FR_OK) || (bytes_written != 512)) { // a full volume gives FR_OK and fewer bytes
		return REC_ERR_WRITE_ERROR;
	}
	printf("$");
    out++;
    if(out == REC_NUM_CACHE_BLOCKS) {
        out = 0;
    }        
    block_out = out;
    blocks_written++;

    return REC_ERR_OK;
}

void TapeRecorder :: flush()
{
    end_capture(true);
}

// Ends the capture: the interrupt is disabled, what has been captured is written out (unless
// write_out is false), the length is put in the header and the file is closed. A write that
// fails ends the writing out, and the error is left in error_code for the task to show.
void TapeRecorder :: end_capture(bool write_out)
{
	if(!file) {
	    error_code = REC_ERR_NO_FILE;
    	recording = 0;
    	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
    	RECORD_CONTROL = 0; // make sure the interrupt has been cleared too.
        return;
    }

    int err = REC_ERR_OK;
    uint32_t bytes_written;

    if (write_out) {
        while ((err == REC_ERR_OK) && (blocks_cached > blocks_written)) {
            err = write_block();
        }

        while ((err == REC_ERR_OK) && (RECORD_STATUS & REC_STAT_BYTE_AV)) {
            uint8_t *block = (uint8_t *)cache_blocks[block_in];
            int i;
            for(i=0;i<512;i++) {
                if (RECORD_STATUS & REC_STAT_BYTE_AV)
                    *(block++) = RECORD_DATA;
                else
                    break;
            }
            printf("Writing out remaining %d bytes.\n", i);
            bytes_written = 0;
            FRESULT res = file->write((void *)cache_blocks[block_in], i, &bytes_written);
            total_length += bytes_written;
            if ((res != FR_OK) || (bytes_written != (uint32_t)i)) {
                err = REC_ERR_WRITE_ERROR;
            }
        }
    }
    if (err != REC_ERR_OK) {
        error_code = err;
    }

    if (file->isValid()) { // a file whose medium is gone only has to be closed
        file->seek(16);
        uint32_t le_size = cpu_to_32le(total_length);
        file->write(&le_size, 4, &bytes_written);
    }

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

	while(1) {
		if(error_code != REC_ERR_OK) {
			PROFILER_SUB = 15;
			if (last_user_interface) {
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

		// The commands that start and finish a capture run on other tasks with this subsystem's
		// mutex held (SubsysCommand::execute), so the capture is only ever finished between two
		// steps of this task, never while a block is being written. The task does not wait for
		// the mutex: it has the highest priority, and what the other tasks do while they hold
		// it (a name box, a popup) can take as long as the user does. The interrupt keeps
		// caching meanwhile, and the cache holds 128 blocks.
		if (trylock()) {
			if (file) { // check for invalidation
				if(!file->isValid()) {
					printf("TapeRecorder: Our file got killed...\n");
					stop(REC_ERR_NO_FILE); // closes the handle too; FileManager::fclose accepts an invalidated file
				}
			}
			if ((recording > 0) && file) {
				if(blocks_cached >= (blocks_written+REC_NUM_CACHE_BLOCKS)) {
					stop(REC_ERR_OVERFLOW);
				}
				while (blocks_cached > blocks_written) {
					int err = write_block();
					if (err != REC_ERR_OK) { // the capture is over; it does not try again
						stop(err);
						break;
					}
				}
			}
			unlock();
		}
		vTaskDelay(10);
	}
}

bool TapeRecorder :: request_file(SubsysCommand *cmd)
{
    char buffer[40] = {0};
    
    if(file) {
        printf("** WARNING.. File variable is already set!!\n");
        recording = 1; // fix so we can stop and close
        return false;
    }
        
    uint32_t dummy = 0;
    const char *signature = "C64-TAPE-RAW\001\0\0\0\0\0\0\0";

	int res = cmd->user_interface->string_box("Give name for tap file..", buffer, 22);
	if ((res > 0) && (*buffer)) {
        total_length = 0;
		set_extension(buffer, ".tap", 32);
		fix_filename(buffer);
        FRESULT fres = fm->fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &file);
        if(fres == FR_OK) {
            FRESULT res = file->write(signature, 20, &dummy);
            if((res != FR_OK) || (dummy != 20)) {
                fm->fclose(file); // no capture will use it; the next attempt must find the member empty
                file = NULL;
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
