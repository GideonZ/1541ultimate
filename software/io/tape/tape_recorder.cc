/*************************************************************/
/* Tape Recorder Control                                     */
/*************************************************************/
#include "tape_recorder.h"
#include "filemanager.h"
#include "userinterface.h"
extern "C" {
    #include "dump_hex.h"
}

TapeRecorder *tape_recorder = NULL; // globally static

#define MENU_REC_SAMPLE_TAPE   0x3211
#define MENU_REC_RECORD_TO_TAP 0x3212
#define MENU_REC_FINISH        0x3213

#define CPU_IRQ_VECTOR      *((DWORD *)0x10)

static void poll_tape_rec(Event &e)
{
	tape_recorder->poll(e);
}

static void tape_req_irq(void)
{
    UART_DATA = 0x2D;
    tape_recorder->irq();
}

__inline DWORD cpu_to_le_32(DWORD a)
{
    DWORD m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
}

TapeRecorder :: TapeRecorder()
{
    recording = 0;
    select = 0;
	file = NULL;
	stop(REC_ERR_OK);
    poll_list.append(&poll_tape_rec);
	main_menu_objects.append(this);
	
	cache = new BYTE[REC_CACHE_SIZE];
    for(int i=0;i<REC_NUM_CACHE_BLOCKS;i++) {
        cache_blocks[i] = (DWORD *)&cache[512*i];
    }
}

TapeRecorder :: ~TapeRecorder()
{
	poll_list.remove(&poll_tape_rec);
	main_menu_objects.remove(this);
	stop(REC_ERR_OK);
    delete[] cache;
}
	
int  TapeRecorder :: fetch_task_items(IndexedList<PathObject*> &item_list)
{
	int items = 0;
    PathObject *po;
    FileInfo *info;
	if(recording) {
		item_list.append(new ObjectMenuItem(this, "Finish Rec. to TAP", MENU_REC_FINISH));
		items = 1;
	}
    else {
        po = user_interface->get_path();

        if(po) {
            printf("Current DIR: %s\n", po->get_name());
            FileInfo *info = po->get_file_info();
            if(info)
                info->print_info();
        } else {
            printf("Path not set.\n");
            return items;
        }

        if(po && po->get_file_info()) {
            info = po->get_file_info();
            if(info->is_writable()) {
        		item_list.append(new ObjectMenuItem(this, "Sample tape to TAP", MENU_REC_SAMPLE_TAPE));
        		item_list.append(new ObjectMenuItem(this, "Capture save to TAP", MENU_REC_RECORD_TO_TAP));
        		items = 2;
            }
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
    	push_event(e_freeze);
    }
	if(file) {	
        printf("Flush TAP..\n");
        flush();
		printf("Closing tape file..\n");
		root.fclose(file);
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
    CPU_IRQ_VECTOR = (DWORD)&tape_req_irq;	// FIXME

	LEAVE_SAFE_SECTION;
	
	push_event(e_unfreeze);
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
    UART_DATA = 0x2B;
}
	
void TapeRecorder :: cache_block()
{
    DWORD *block = cache_blocks[block_in];
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
	
	UINT bytes_written;

    DWORD *block = cache_blocks[block_out];
	file->write((void *)block, 512, &bytes_written);
    total_length += 512;
	if(bytes_written == 0) {
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

	UINT bytes_written;
	while(RECORD_STATUS & REC_STAT_BYTE_AV) {
		file->write((void *)RECORD_DATA, 1, &bytes_written);
        total_length ++;
	}
	
    file->seek(16);
    DWORD le_size = cpu_to_le_32(total_length);
    file->write(&le_size, 4, &bytes_written);

	root.fclose(file);
	push_event(e_reload_browser);
	file = NULL;

	recording = 0;
	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
	RECORD_CONTROL = 0; // make sure the interrupt has been cleared too.
}
	
	
void TapeRecorder :: poll(Event &e)
{
    if((error_code != REC_ERR_OK) && user_interface->is_available()) {
        user_interface->popup("Error during tape capture.", BUTTON_OK);
        error_code = REC_ERR_OK;
    } else if ((recording >0) && (e.type == e_enter_menu)) {
        if(user_interface->popup("End tape capture?", BUTTON_YES | BUTTON_NO) == BUTTON_YES)
            flush();
    }        
	if(e.type == e_object_private_cmd) {
		if(e.object == this) {
			switch(e.param) {
				case MENU_REC_FINISH:
					flush();
					break;
				case MENU_REC_SAMPLE_TAPE:
					select = 0;
					if(request_file())
						start();
					break;
				case MENU_REC_RECORD_TO_TAP:
					select = 1;
					if(request_file())
						start();
					break;
				default:
					break;
			}
		}
	}
	if (file) { // check for invalidation
	    if (!file->node) {
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
}

bool TapeRecorder :: request_file(void)
{
    char buffer[40];
    
    if(file) {
        printf("** WARNING.. File variable is already set!!\n");
        recording = 1; // fix so we can stop and close
        return false;
    }
        
    UINT dummy;
    char *signature = "C64-TAPE-RAW\001\0\0\0\0\0\0\0";

	int res = user_interface->string_box("Give name for tap file..", buffer, 22);
	if(res > 0) {
        total_length = 0;
		set_extension(buffer, ".tap", 32);
		fix_filename(buffer);
        file = root.fcreate(buffer, user_interface->get_path());
        if(file) {
            file->write(signature, 20, &dummy);
            if(dummy != 20) {
                user_interface->popup("Error writing signature", BUTTON_OK);
                return false;
            }
            return true;
        } else {
            user_interface->popup("Error opening file", BUTTON_OK);
        }            
	}
	return false;
 }
