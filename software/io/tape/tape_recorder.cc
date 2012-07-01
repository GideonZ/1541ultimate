/*************************************************************/
/* Tape Recorder Control                                     */
/*************************************************************/
#include "tape_recorder.h"
#include "filemanager.h"
#include "userinterface.h"

TapeRecorder *tape_recorder = NULL; // globally static

#define MENU_REC_SAMPLE_TAPE   0x3211
#define MENU_REC_RECORD_TO_TAP 0x3212
#define MENU_REC_FINISH        0x3213

static void poll_tape_rec(Event &e)
{
	tape_recorder->poll(e);
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
	stop();
    poll_list.append(&poll_tape_rec);
	main_menu_objects.append(this);
}

TapeRecorder :: ~TapeRecorder()
{
	poll_list.remove(&poll_tape_rec);
	main_menu_objects.remove(this);
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

void TapeRecorder :: stop()
{
	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
	RECORD_CONTROL = 0;

	if(file) {	
        printf("Flush TAP..\n");
        flush();
		printf("Closing tape file..\n");
		root.fclose(file);
	}
	file = NULL;
}
	
void TapeRecorder :: start()
{
	printf("Start recording.. Status = %b.\n", RECORD_STATUS);
	RECORD_CONTROL = REC_CLEAR_ERROR | REC_FLUSH_FIFO;
	RECORD_CONTROL = REC_MODE_FALLING;
	
	if(select)
	    RECORD_CONTROL = REC_ENABLE | REC_MODE_RISING | REC_SELECT_WRITE;
	else
	    RECORD_CONTROL = REC_ENABLE | REC_MODE_FALLING | REC_SELECT_READ;

	recording = 1;
}
	
void TapeRecorder :: write_block()
{
	if(!file)
		return;
	
	UINT bytes_written;

	file->write((void *)RECORD_DATA, block, &bytes_written);
    total_length += block;
	if(bytes_written == 0) {
		stop();
	}
	printf("$");
	block = 512;
}

void TapeRecorder :: flush()
{
	if(!file)
		return;

	while(RECORD_STATUS & REC_STAT_BLOCK_AV)
		write_block();

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
}
	
	
void TapeRecorder :: poll(Event &e)
{
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
		
	BYTE st = RECORD_STATUS;
	if(st & REC_STAT_ENABLED) { // we are enabled
		if(st & REC_STAT_BLOCK_AV) {
			write_block();
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
		set_extension(buffer, ".tap", 32);
		fix_filename(buffer);
        file = root.fcreate(buffer, user_interface->get_path());
        if(file)
            file->write(signature, 20, &dummy);
        block = 512 - 20;
        total_length = 0;
        return (file != NULL);
	}
	return false;
 }
