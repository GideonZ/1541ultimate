#ifndef IEC_CHANNEL_H
#define IEC_CHANNEL_H

#include "integer.h"
#include "iec.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "filemanager.h"
#include "mystring.h"

typedef enum _t_channel_state {
    e_idle, e_filename, e_file, e_dir, e_complete, e_error
    
} t_channel_state;

enum t_channel_retval {
    IEC_OK=0, IEC_LAST=1, IEC_NO_DATA=-1, IEC_FILE_NOT_FOUND=-2, IEC_NO_FILE=-3,
    IEC_READ_ERROR=-4, IEC_WRITE_ERROR=-5, IEC_BYTE_LOST=-6 
};

static uint8_t c_header[32] = { 1,  1,  4,  1,  0,  0, 18, 34,
                            32, 32, 32, 32, 32, 32, 32, 32,
                            32, 32, 32, 32, 32, 32, 32, 32,
                            34, 32, 48, 48, 32, 50, 65,  0 };

class IecCommandChannel;

class IecChannel
{
    IecInterface *interface;
    FileManager *fm;

	int  channel;
    int  write;
    uint8_t buffer[256];
    int  size;
    int  pointer;
    File *f;
    int  last_command;
    int  dir_index;
    int  dir_last;
    
    t_channel_state state;

    int  last_byte;

    // temporaries
    uint32_t bytes;

public:
    IecChannel(IecInterface *intf, int ch)
    {
    	fm = FileManager :: getFileManager();
    	interface = intf;
        channel = ch;
        f = NULL;
        pointer = 0;
        write = 0;
        state = e_idle;
        dir_index = 0;
        dir_last = 0;
        bytes = 0;
        last_byte = 0;
        size = 0;
        last_command = 0;
    }
    
    virtual ~IecChannel()
    {
    	close_file();
    }


    virtual int pop_data(uint8_t& b)
    {
        switch(state) {
            case e_file:
                if(pointer == 256) {
                    if(read_block())
                        return IEC_READ_ERROR;
                }            
                break;
            case e_dir:                
                if(pointer == 32) {
                    if(read_dir_entry())
                        return IEC_READ_ERROR;
                }
                break;
//            case e_complete:
//                b = 0;
//                return IEC_LAST;
            default:
                b = 0;
                return IEC_NO_FILE;
        }

        if(pointer == last_byte) {
            b = buffer[pointer];
            state = e_complete;
            return IEC_LAST;
        }
        b = buffer[pointer++];
        return IEC_OK;
    }
    
    int read_dir_entry(void)
    {
        if(dir_index >= dir_last) {
            buffer[2] = 9999 & 255;
            buffer[3] = 9999 >> 8;
            memcpy(&buffer[4], "BLOCKS FREE.             \0\0\0", 28);
            last_byte = 31;
            pointer = 0;
            return 0;
        }
        printf("Dir index = %d %s\n", dir_index, (*interface->iecNames)[dir_index]);
        FileInfo *info = (*interface->dirlist)[dir_index];

        //printf("info = %p\n", info);
        uint32_t size = 0;
        uint32_t size2 = 0;
        if(info) {
            size = info->size;
        }
        size /= 254;
        if(size > 9999)
            size = 9999;
        size2 = size;
        int chars=1;
        while(size2 >= 10) {
            size2 /= 10;
            chars ++;
        }        
        int spaces=3-chars;
        buffer[2] = size & 255;
        buffer[3] = size >> 8;
        int pos = 4;
        while((spaces--)>=0)
            buffer[pos++] = 32;
        buffer[pos++]=34;
        char *name = (*interface->iecNames)[dir_index];
        while(*name)
            buffer[pos++] = *(name++);
        buffer[pos++] = 34;
        while(pos < 32)
            buffer[pos++] = 32;

        if (info->is_directory()) {
        	memcpy(&buffer[27-chars], "DIR", 3);
        } else if(strcmp(info->extension, "PRG") != 0) {
        	memcpy(&buffer[27-chars], "SEQ", 3);
        } else {
        	memcpy(&buffer[27-chars], "PRG", 3);
        }
        buffer[31] = 0;
        pointer = 0;
        dir_index ++;
        return 0;
    }
    
    int read_block(void)
    {
        FRESULT res = FR_DENIED;
        uint32_t bytes;
        if(f) {
            res = f->read(buffer, 256, &bytes);
            printf("\nSize was %d. Read %d bytes. ", size, bytes);
        }
        if(res != FR_OK) {
            state = e_error;
            return IEC_READ_ERROR;
        }
        if(bytes == 0)
            state = e_complete; // end should have triggered already
            
        if(bytes != 256) {
            last_byte = int(bytes)-1;
        } else if(size == 256) {
            last_byte = 255;
        }
        size -= 256;
        pointer = 0;
        return 0;
    }
            
    virtual int push_data(uint8_t b)
    {
        uint32_t bytes;

        switch(state) {
            case e_filename:
                if(pointer < 64)
                    buffer[pointer++] = b;
                break;    
            case e_file:
                buffer[pointer++] = b;
                if(pointer == 256) {
                    FRESULT res = FR_DENIED;
                	if(f) {
                        res = f->write(buffer, 256, &bytes);
                    }
                    if(res != FR_OK) {
                        fm->fclose(f);
                        f = NULL;
                        state = e_error;
                        return IEC_WRITE_ERROR;
                    }
                    pointer = 0;
                }
                return IEC_BYTE_LOST;

            default:
                return IEC_BYTE_LOST;
        }
        return IEC_OK;
    }
    
    virtual int push_command(uint8_t b)
    {
        if(b)
            last_command = b;

        switch(b) {
            case 0xF0: // open
                close_file();
                state = e_filename;
                pointer = 0;
                break;
            case 0xE0: // close
                printf("close %d %d\n", pointer, write);
                if(write) {
                    dump_hex(buffer, pointer);
                    if (f) {
                    	if (pointer > 0) {
                    		FRESULT res = f->write(buffer, pointer, &bytes);
                    		if (res != FR_OK) {
                                state = e_error;
                                return IEC_WRITE_ERROR;
                    		}
                    	}
                        close_file();
                    } else {
                        state = e_error;
                        return IEC_WRITE_ERROR;
                    }
                }            
                state = e_idle;
                break;
            case 0x60:
                break;
            case 0x00: // end of data
                if(last_command == 0xF0)
                    open_file();
                break;
            default:
                printf("Error on channel %d. Unknown command: %b\n", channel, b);
        }
        return IEC_OK;
    }
    
private:
    int open_file(void)  // name should be in buffer
    {
        char *extension;
        
        buffer[pointer] = 0; // string terminator
        printf("Open file. Raw Filename = '%s'\n", buffer);

        // defaults
        if(channel == 1)
            write = 1;
        else
            write = 0;
        if(channel < 2)
            extension = ".prg";
        else
            extension = ".seq";

        // parse filename parameters, look for ,                       
        for(int i=0;i<pointer;i++) {
            if(buffer[i] == ',') {
                buffer[i] = 0;
                switch(buffer[i+1]) {
                    case 'R':
                        write = 0;
                        break;
                    case 'W':
                        write = 1;
                        break;
                    case 'P':
                        extension = ".prg";
                        break;
                    case 'S':
                        extension = ".seq";
                        break;
                    case 'U':
                        extension = ".usr";
                        break;
                    default:
                        printf("Unknown control char in filename: %c\n", buffer[i+1]);
                }
                buffer[i] = tolower(buffer[i]);
                break;
            }
        }
        printf("Filename after parsing: '%s'. Extension = '%s'. Write = %d\n", buffer, extension, write);

        uint8_t flags = FA_READ;

        if(buffer[0] == '$') {
            printf("IEC Channel: Opening directory...\n");
            interface->readDirectory();
            state = e_dir;
            pointer = 0;
            last_byte = -1;
            size = 32;
            memcpy(buffer, c_header, 32);

            const char *name = interface->path->get_path();
            int pos = 8;
            while((pos < 23) && (*name))
                buffer[pos++] = toupper(*(name++));
            dump_hex(buffer, 32);
            dir_last = interface->dirlist->get_elements();
            dir_index = 0;
            return 0;
        }

        interface->readDirectory();
        int pos = interface->findIecName((char *)buffer, extension);

        char *filename;

        if(write) {
        	if (pos >= 0) {
        		interface->last_error = ERR_FILE_EXISTS;
        		state = e_error;
        		return 0;
        	} else {
        		strcat((char *)buffer, extension);
        		filename = (char *)buffer;
        	}
        } else { // read
            if (pos < 0) {
            	interface->last_error = ERR_FILE_NOT_FOUND;
        		state = e_error;
            	return 0;
            }
            FileInfo *info = (*interface->dirlist)[pos];
            filename = info->lfname;
        }

        f = fm->fopen(interface->path, filename, (write)?(FA_WRITE|FA_CREATE_NEW|FA_CREATE_ALWAYS):(FA_READ));
        if(f) {
            printf("Successfully opened file %s in %s\n", buffer, interface->path->get_path());
            last_byte = -1;
            pointer = 0;
            state = e_file;
            if(!write) {
                size = f->get_size();
                return read_block();
            }
        } else {
            printf("Can't open file %s in %s\n", buffer, interface->path->get_path());
    		state = e_error;
        }            
        return 0;
    }
    
    
    int close_file(void) // file should be open
    {
        if(f)
            fm->fclose(f);
        f = NULL;
        state = e_idle;
        return 0;
    }
    friend class IecCommandChannel;
};

class IecCommandChannel : public IecChannel
{
//    BYTE error_buf[40];
    int track_counter;
public:
    IecCommandChannel(IecInterface *intf, int ch) : IecChannel(intf, ch)
    {
        get_last_error();
    }
    ~IecCommandChannel() { }

    void get_last_error(int err = -1)
    {
        if(err >= 0)
            interface->last_error = err;

        last_byte = interface->get_last_error((char *)buffer) - 1;
        printf("Get last error: last = %d. buffer = %s.\n", last_byte, buffer);
        pointer = 0;
    	interface->last_error = ERR_OK;
    }

    int pop_data(uint8_t& b)
    {
        if(pointer > last_byte) {
            return IEC_NO_FILE;
        }
        if(pointer == last_byte) {
            b = buffer[pointer];
            get_last_error();
            return IEC_LAST;
        }
        b = buffer[pointer++];
        return IEC_OK;
    }

    int push_data(uint8_t b)
    {
        if(pointer < 64) {
            buffer[pointer++] = b;
            return IEC_OK;
        }
        return IEC_BYTE_LOST;
    }

    int push_command(uint8_t b)
    {
        switch(b) {
            case 0x60:
            case 0xE0:
            case 0xF0:
                pointer = 0;
                break;
            case 0x00: // end of data, command received in buffer
                buffer[pointer]=0;
                printf("Command received: %s\n", buffer);
                if (strncmp((char *)buffer, "CD:", 3) == 0) {
                	cd((char *)&buffer[3]);
                } else { // unknown command
                	get_last_error(ERR_SYNTAX_ERROR_CMD);
                }
                break;
            default:
                printf("Error on channel %d. Unknown command: %b\n", channel, b);
        }
        return IEC_OK;
    }

    bool cd(char *name) {
    	int p = interface->findIecName(name, "DIR");
    	if (p < 0) {
    		if(!interface->path->cd(name)) {
    			get_last_error(ERR_FILE_NOT_FOUND);
    			return false;
    		} else {
    			return true;
    		}
    	}
    	FileInfo *info = (*interface->dirlist)[p];
    	if (!info->is_directory()) {
        	get_last_error(ERR_FILE_TYPE_MISMATCH);
        	return false;
    	}

    	if (!interface->path->cd(info->lfname)) {
        	get_last_error(ERR_DIRECTORY_ERROR);
        	return false;
    	}
    	return true;
    }
};

#endif /* IEC_CHANNEL_H */
