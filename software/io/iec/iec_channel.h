#ifndef IEC_CHANNEL_H
#define IEC_CHANNEL_H

#include "integer.h"
#include "filemanager.h"
#include "iec.h"
#include <ctype.h>

typedef enum _t_channel_state {
    e_idle, e_filename, e_file, e_complete, e_error
    
} t_channel_state;

enum t_channel_retval {
    IEC_OK=0, IEC_LAST=1, IEC_NO_DATA=-1, IEC_FILE_NOT_FOUND=-2, IEC_NO_FILE=-3,
    IEC_READ_ERROR=-4, IEC_WRITE_ERROR=-5, IEC_BYTE_LOST=-6 
};

class IecChannel
{
    IecInterface *interface;
    int  channel;
    int  write;
    BYTE buffer[256];
    int  size;
    int  pointer;
    File *f;
    int  last_command;
    t_channel_state state;

    int  last_byte;

//    int  flags; // read, write, dirty
//    int  block, last_block;

    // temporaries
    UINT bytes;
    FRESULT res;

public:
    IecChannel(IecInterface *intf, int ch)
    {
        interface = intf;
        channel = ch;
        f = NULL;
        pointer = 0;
        write = 0;
        state = e_idle;
    }
    
    ~IecChannel()
    {
        close_file();
    }

    int pop_data(BYTE& b)
    {
        if(state != e_file) {
            return IEC_NO_FILE;
        }
        if(pointer == 256) {
            if(!read_block())
                return IEC_READ_ERROR;
        }            
        if(pointer == last_byte) {
            b = buffer[pointer];
            state = e_complete;
            return IEC_LAST;
        }
        b = buffer[pointer++];
        return IEC_OK;
    }
    
    int read_block(void)
    {
        res = f->read(buffer, 256, &bytes);
        printf("\nSize was %d. Read %d bytes. ", size, bytes);
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
    }
            
    int push_data(BYTE b)
    {
        UINT bytes;
        FRESULT res;

        switch(state) {
            case e_filename:
                if(pointer < 64)
                    buffer[pointer++] = b;
                break;    
            case e_file:
                buffer[pointer++] = b;
                if(pointer == 256) {
                    res = f->write(buffer, 256, &bytes);
                    if(res != FR_OK) {
                        root.fclose(f);
                        state = e_error;
                        return IEC_WRITE_ERROR;
                    }
                    pointer = 0;
                }
            default:
                return IEC_BYTE_LOST;
        }
        return IEC_OK;
    }
    
    int push_command(BYTE b)
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
                if((pointer > 0) && (write)) {
                    dump_hex(buffer, pointer);
                    res = f->write(buffer, pointer, &bytes);
                    root.fclose(f);
                    if(res != FR_OK) {
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
            } else {
                buffer[i] = BYTE(tolower(int(buffer[i])));
            }
        }
        printf("Filename after parsing: '%s'. Extension = '%s'. Write = %d\n", buffer, extension, write);
        strcat((char *)buffer, extension);

        PathObject *dir = interface->path->get_path_object();
        BYTE flags = FA_READ;
        if(write)
            flags |= (FA_WRITE | FA_CREATE_NEW);
            
        f = root.fopen((char *)buffer, dir, flags);
        if(f) {
            printf("Successfully opened file %s in %s\n", buffer, interface->path->get_path());
            last_byte = -1;
            pointer = 0;
            state = e_file;
            if(!write) {
                size = f->get_size();
                read_block();
            }
        } else {
            printf("Can't open file %s in %s\n", buffer, interface->path->get_path());
        }            
    }
    
    
    
    int close_file(void) // file should be open
    {
        if(f)
            root.fclose(f);
        f = NULL;
        state = e_idle;
    }
};

/*
class IecCommandChannel : public IecChannel
{
public:
    IecCommandChannel() {}
    ~IecCommandChannel() {}
};
*/
#endif /* IEC_CHANNEL_H */
