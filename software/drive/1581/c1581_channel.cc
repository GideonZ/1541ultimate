#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "c1581.h"
#include "c1581_channel.h"
#include "../io/iec/iec.h"



C1581_Channel::C1581_Channel()
{
	c1581 = NULL;
	channel = 0;
	channelopen = false;
	pointer = 0;
	state = e_idle;
	last_byte = 0;
	last_command = 0;
	prefetch_max = 256;
	prefetch = 0;
	size=0;
	write=0;
	blocknumber = 0;
	writesector = 0;
	writetrack = 0;
	writeblock = 0;
}

C1581_Channel::~C1581_Channel()
{
	c1581 = NULL;
}

void C1581_Channel::init(C1581 *parent, int ch)
{
	c1581 = parent;
	channel = ch;

	memset(buffer,0,256);
}

void C1581_Channel :: reset_prefetch(void)
{
	prefetch = pointer;
}

int C1581_Channel :: push_data(uint8_t b)
{
    static uint8_t zctr;

	switch(state) {
		case e_filename:
			if(pointer < 64)
				buffer[pointer++] = b;
			break;
		case e_block:
			buffer[pointer++] = b;
			break;
		case e_file:
			buffer[pointer++] = b;

			if(pointer == BLOCK_SIZE) {
				uint8_t newTrack;
				uint8_t newSector;

				c1581->findFreeSector(&newTrack, &newSector);
				c1581->setTrackSectorAllocation(newTrack, newSector, true);
				c1581->goTrackSector(writetrack, writesector);

				c1581->sectorBuffer[0] = newTrack;
				c1581->sectorBuffer[1] = newSector;

				for(int t = 2; t< BLOCK_SIZE; t++)
					c1581->sectorBuffer[t] = buffer[t];

				writesector = newSector;
				writeblock++;
				pointer = 2;

			}
			return IEC_OK;

		default:
			return IEC_BYTE_LOST;
	}
	return IEC_OK;
}

int C1581_Channel :: push_command(uint8_t b)
{
    if(b)
        last_command = b;

    switch(b) {
        case 0xF0: // open
            //close_file();

        	writeblock=0;
			write=0;
			pointer=0;
			writetrack = 0;
			writesector = 0;
			buffer[0] = 0;
			blocknumber=0;
			size=0;
			localbuffer[0]=0;
			file_name[0]=0;
			last_byte = 0;
			prefetch = 0;
			prefetch_max =0;

            state = e_filename;
            pointer = 0;
            break;
        case 0xE0: // close
            //printf("close %d %d\n", pointer, write);
            if(write) {
            	close_file();
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

int C1581_Channel :: prefetch_data(uint8_t& data)
{
    if (state == e_error) {
        return IEC_NO_FILE;
    }
    if (prefetch == last_byte) {
    	data = buffer[prefetch];
        prefetch++;
        return IEC_LAST;
    }
    if (prefetch < prefetch_max) {
    	data = buffer[prefetch];
		prefetch++;
		return IEC_OK;
    }
    if (prefetch > prefetch_max) {
        return IEC_NO_FILE;
    }

    return IEC_BUFFER_END;
}

int C1581_Channel :: pop_data(void)
{
    switch(state) {
    	case e_dir:
        case e_file:
            if(pointer == last_byte)
            {
                state = e_complete;
                return IEC_NO_FILE; // no more data?
            }
            else if(pointer == BLOCK_SIZE-1) {
				if(read_block())  // also resets pointer.
					return IEC_READ_ERROR;
				else
					return IEC_OK;
			}
            // fix for seq read bug
            if(prefetch > last_byte)
            {
            	prefetch = pointer+1;
            }
            break;
        case e_block:
        	// channel will remain in the e_block state
            if(pointer == last_byte)
            {
            	pointer = 0;
            	prefetch = 0;
                return IEC_NO_FILE; // no more data
            }
            if(prefetch > last_byte)
			{
				prefetch = pointer+1;
			}
            break;
        default:
            return IEC_NO_FILE;
    }

    pointer++;
    return IEC_OK;
}

void C1581_Channel :: parse_command(char *buffer, command_t *command)
{
    // initialize
    for(int i=0;i<5;i++) {
        command->names[i].name = 0;
        command->names[i].drive = -1;
        command->names[i].separator = ' ';
    }
    command->digits = -1;
    command->cmd = buffer;

    // First strip off any carriage returns, in any case
    int len = strlen(buffer);
    while ((len > 0) && (buffer[len-1] == 0x0d)) {
        len--;
        buffer[len] = 0;
    }

    // look for , and split
    int idx = 0;
    command->names[0].name = buffer;
    for(int i=0;i<len;i++) {
        if ((buffer[i] == ',')||(buffer[i] == '=')) {
            idx++;
            if (idx == 5) {
                break;
            }
            command->names[idx].name = buffer + i + 1;
            command->names[idx].separator = buffer[i];
            buffer[i] = 0;
        }
    }

    // user commands should be interpreted by the caller
    if(command->cmd[0] == 'U')
    	return;

    // look for : and split
    for(int j=0;j<5;j++) {
        char *s = command->names[j].name;
        if (!s) {
            break;
        }
        len = strlen(s);
        for(int i=0;i<len;i++) {
            if (s[i] == ':') {
                s[i] = 0;
                command->names[j].name = s + i + 1;
                // now snoop off the digits and place them into drive number
                int mult = 1;
                int dr = 0;
                while(isdigit(s[--i])) {
                    dr += (s[i] - '0') * mult;
                    mult *= 10;
                    s[i] = 0;
                }
                if (mult > 1) { // digits found
                    command->names[j].drive = dr;
                }
                break;
            }
        }
    }
    if (command->names[0].name == command->cmd) {
        command->names[0].name = 0;
        int mult = 1;
        int dr = 0;
        char *s = command->cmd;
        int i = strlen(s);
        while(isdigit(s[--i])) {
            dr += (s[i] - '0') * mult;
            mult *= 10;
            //s[i] = 0; // we should not snoop them off; they may be part of a filename
        }
        if (mult > 1) { // digits found
            command->digits = dr;
        }
    }
}

bool C1581_Channel :: hasIllegalChars(const char *name)
{
    int len = strlen(name);
    for(int i=0;i<len;i++) {
        switch(name[i]) {
        case '/': return true;
        case '$': return true;
        case '?': return true;
        case '*': return true;
        case ',': return true;
        default: break;
        }
    }
    return false;
}

const char *C1581_Channel :: GetExtension(char specifiedType, bool &explicitExt)
{
    explicitExt = false;
    const char *extension = "";
    switch(specifiedType) {
    case 'P':
        extension = ".prg";
        explicitExt = true;
        break;
    case 'S':
        extension = ".seq";
        explicitExt = true;
        break;
    case 'U':
        extension = ".usr";
        explicitExt = true;
        break;
    case 'D':
        extension = ".dir";
        explicitExt = true;
        break;
    default:
        printf("Unknown filetype: %c\n", specifiedType);
    }
    return extension;
}

uint8_t C1581_Channel::open_file(void)
{
	const char *extension;

	buffer[pointer] = 0; // string terminator
	printf("Open file. Raw Filename = '%s'\n", buffer);

	command_t command;
	parse_command((char *)buffer, &command);

	// First determine the direction
	write = 0;
	if(channel == 1)
		write = 1;

	int tp = 1;
	if (command.names[1].name) {
		switch(command.names[1].name[0]) {
		case 'R':
			write = 0;
			tp = 2;
			break;
		case 'W':
			write = 1;
			tp = 2;
			break;
		default:
			printf("Unknown direction: %c\n", command.names[2].name[0]);
		}
	}

	if ((command.names[2].name) && (tp == 1)) {
		switch(command.names[2].name[0]) {
		case 'R':
			write = 0;
			break;
		case 'W':
			write = 1;
			break;
		default:
			printf("Unknown direction: %c\n", command.names[2].name[0]);
		}
	}

	// Then determine the type (or extension) to be used
	bool explicitExt = false;
	if(channel < 2) {
		extension = ".prg";
	} else if (write) {
		extension = ".seq";
	} else {
		extension = ".???";
	}

	if (command.names[tp].name) {
		extension = GetExtension(command.names[1].name[0], explicitExt);
	}

	const char *first = (command.names[0].name) ? command.names[0].name : "*";
	printf("Filename after parsing: '%s' %d:%s. Extension = '%s'. Write = %d\n", command.cmd, command.digits, first, extension, write);

	uint8_t track;
	uint8_t sector;

	pointer = 2;

	channelopen = true;

	if (c1581->disk_state == e_no_disk81)
	{
		state = e_error;
		return ERR_DRIVE_NOT_READY;
	}

	if (command.cmd[0] == '#')
	{
		// Block read command
		pointer = 0;
		state = e_block;
		return ERR_OK;
	}

	if (command.cmd[0] == '$')
	{
		if (explicitExt) {
			dirpattern = &extension[1]; // skip the .
		} else {
			dirpattern = "???";
		}

		dirpattern += first;

		state = e_dir;
		last_byte = c1581->get_directory(localbuffer);
		blocknumber = 0;
		pointer = 0;
		read_block();
		return ERR_OK;
	}

	bool replace = false;
	const char *rawName;
	// If a drive number is given, there is a : in the name, and therefore the replacement @ will end up in the command
	// and the filename ends up in names[0]. In case no drive number is given, the filename is inside of the command, and may still be preceded with @.
	// In both cases, the @ ends up in the first char of the command!
	rawName = command.cmd; // by default we assume that the filename goes in the command
	if (*rawName == '@') {
		replace = true;
		rawName ++; // for now we assume that the filename goes after the @.
	}
	// if a : was present, the actual filename (without @) appears in names[0]
	if (command.names[0].name) {
		rawName = command.names[0].name;
	}

	strcpy((char *)file_name, rawName);
	state = e_file;

	if(write)
	{
		uint8_t track;
		uint8_t sector;
		uint8_t ftype;

		if(strcmp(extension, ".del") == 0)
			ftype = 0;
		else if(strcmp(extension, ".seq") == 0)
			ftype = 1;
		else if(strcmp(extension, ".prg") == 0)
			ftype = 2;
		else if(strcmp(extension, ".usr") == 0)
			ftype = 3;
		else if(strcmp(extension, ".rel") == 0)
			ftype = 4;
		else if(strcmp(extension, ".cbm") == 0)
			ftype = 5;

		// Check if file exists
		uint8_t tmptrack, tmpsector;
		int err = c1581->getFileTrackSector((char *)file_name, &tmptrack, &tmpsector, false);

		if(err == ERR_FILE_NOT_FOUND)
		{
			// Create directory entry and allocate
			err = c1581->createDirectoryEntry((char *)file_name, ftype, &track, &sector);

			if(err != ERR_OK)
			{
				state = e_error;
				return err;
			}

			writeblock = 1;
			writetrack = track;
			writesector = sector;
			pointer=2;
		}
	}
	else
	{
		int err = c1581->readFile(file_name, localbuffer, &last_byte);

		if(err != ERR_OK)
		{
			state = e_error;
			return err;
		}

		blocknumber = 0;
		read_block();
	}
	return ERR_OK;
}

int C1581_Channel::read_block(void)
{
	memcpy(buffer, &localbuffer[blocknumber * BLOCK_SIZE], BLOCK_SIZE);

	if(state == e_file)
	{
		pointer = 2;
		prefetch = 2;

		// last track
		if(buffer[0] == 0)
		{
			blocknumber = 0;
			last_byte = buffer[1];
		}
	}
	if(state == e_dir)
	{
		pointer = 0;
		prefetch = 0;

		// check for end of file (three zeros)
		for(int t=0;t<254;t++)
		{
			int tz=buffer[t]+buffer[t+1]+buffer[t+2];
			if(tz==0)
			{
				last_byte = t+2;
				blocknumber = 0;
				break;
			}
		}
	}

	prefetch_max = BLOCK_SIZE;

	blocknumber++;
	return ERR_OK;
}

uint8_t C1581_Channel::close_file()
{
	channelopen = false;

	if(write)
	{
		c1581->goTrackSector(writetrack, writesector);
		c1581->sectorBuffer[0] = 0x00;		// last track
		c1581->sectorBuffer[1] = pointer-1;	// last byte

		for(int t = 2; t< pointer; t++)
			c1581->sectorBuffer[t] = buffer[t];

		c1581->updateFileInfo((char *)file_name, writeblock);
		c1581->write_d81();

	}

	writeblock=0;
	write=0;
	pointer=0;
	writetrack = 0;
	writesector = 0;
	buffer[0] = 0;
	blocknumber=0;
	size=0;
	localbuffer[0]=0;
	file_name[0]=0;
	last_byte = 0;
	prefetch = 0;
	prefetch_max =0;
	state = e_idle;
	return 0;
}

// Command Channel

C1581_CommandChannel::C1581_CommandChannel()
{

}
C1581_CommandChannel::~C1581_CommandChannel()
{

}

void C1581_CommandChannel :: init(C1581 *parent, int ch)
{
	c1581 = parent;
	channel = ch;
	get_last_error(ERR_DOS,0,0);
}

void C1581_CommandChannel :: get_last_error(int err = -1, int track = 0, int sector = 0)
{
    if(err >= 0)
        c1581->last_error = err;

    last_byte = c1581->get_last_error((char *)buffer, track, sector) - 1;
    printf("Get last error: last = %d. buffer = %s.\n", last_byte, buffer);
    pointer = 0;
    prefetch = 0;
    prefetch_max = last_byte;
    c1581->last_error = ERR_OK;
}

int C1581_CommandChannel :: pop_data(void)
{
    if(pointer > last_byte) {
        return IEC_NO_FILE;
    }
    if(pointer == last_byte) {
        get_last_error();
        return IEC_LAST;
    }
    pointer++;
    return IEC_OK;
}
int C1581_CommandChannel :: push_data(uint8_t b)
{
    if(pointer < 64) {
        buffer[pointer++] = b;
        return IEC_OK;
    }
    return IEC_BYTE_LOST;
}
int C1581_CommandChannel :: push_command(uint8_t b)
{
    command_t command;

    switch(b) {
        case 0x60:
        case 0xE0:
        case 0xF0:
            pointer = 0;
            break;
        case 0x00: // end of data, command received in buffer
            buffer[pointer]=0;
            // printf("Command received:\n");
            // dump_hex(buffer, pointer);
            parse_command((char *)buffer, &command);
            //dump_command(command);
            exec_command(command);
            break;
        default:
            printf("Error on channel %d. Unknown command: %b\n", channel, b);
    }
    return IEC_OK;
}


void C1581_CommandChannel :: exec_command(command_t &command)
{

	if (strncmp(command.cmd , "NEW", strlen(command.cmd)) == 0) {
        format(command);
    } else if (strncmp(command.cmd, "COPY", strlen(command.cmd)) == 0) {
        copy(command);
    } else if (strncmp(command.cmd, "RENAME", strlen(command.cmd)) == 0) {
        renam(command);
    } else if (strncmp(command.cmd, "SCRATCH", strlen(command.cmd))== 0) {
        scratch(command);
    } else if (strncmp(command.cmd, "VALIDATE", strlen(command.cmd))== 0) {
        validate(command);
    } else if ((strncmp(command.cmd, "INITIALIZE", strlen(command.cmd)) == 0) || (strcmp(command.cmd , "I0") == 0) || (strcmp(command.cmd , "U0>B0") == 0)) {
        get_last_error(ERR_OK,0,0);
    } else if ((strncmp(command.cmd, "UI", 2) == 0) || (strncmp(command.cmd, "UJ", 2) == 0) || (strncmp(command.cmd, "U9", 2) == 0)) {
        get_last_error(ERR_DOS,0,0);
    } else if ((strncmp(command.cmd, "U1", 2) == 0) || (strncmp(command.cmd, "UA", 2) == 0)) {
    	u1(command);
    } else if ((strncmp(command.cmd, "U2", 2) == 0) || (strncmp(command.cmd, "UB", 2) == 0)) {
        u2(command);
    } else if (strncmp(command.cmd, "M-R", 3) == 0) {
        mem_read(command);
    } else if (strncmp(command.cmd, "M-W", 3) == 0) {
        mem_write(command);
    } else if (strncmp(command.cmd, "U0>", 3) == 0) {
        change_devicenum(command);
    } else if (command.cmd[0] == 'M' && command.cmd[1] == '-' && command.cmd[2] == 'E') {
        mem_exec(command);
    } else if (command.cmd[0] == 'B' && command.cmd[1] == '-' && command.cmd[2] == 'A') {
        block_allocate(command);
    } else if (command.cmd[0] == 'B' && command.cmd[1] == '-' && command.cmd[2] == 'F') {
        block_free(command);
    } else if (command.cmd[0] == 'B' && command.cmd[1] == '-' && command.cmd[2] == 'P') {
        block_pointer(command);
    } else if (command.cmd[0] == '/') {
    	create_select_partition(command);
    }
    else { // unknown command
        get_last_error(ERR_SYNTAX_ERROR_CMD,0,0);
    }
}

void C1581_CommandChannel :: format(command_t& command)
{
	char name[32];
	char id[3];
	int  part;
	uint8_t tmp[256];
	bool softFormat = true;

	if (!command.names[0].name) {
		get_last_error(ERR_SYNTAX_ERROR_CMD,0,0);
		return;
	}

	strcpy(name, command.names[0].name);
	part = command.names[0].drive;

	// if there is an ID, perform a hard format
	if (command.names[1].name) {

		if(command.names[1].separator != ',')
		{
			get_last_error(ERR_SYNTAX_ERROR_CMD,0,0);
			return;
		}

		softFormat = false;
		strcpy(id, command.names[1].name);
	}

	if(c1581->disk_state == e_no_disk81)
	{
		get_last_error(ERR_DRIVE_NOT_READY, 40, 0);
		return;
	}

	// clear a tmp buffer
	for(int t=0;t < 256;t++)
		tmp[t] = 0;

	if (softFormat == false)
	{
		// Clear each track / sector
		for(int trk=1; trk<81; trk++)
			for(int sec=0; sec<40; sec++)
			{
				c1581->goTrackSector(trk, sec);
				memcpy(c1581->sectorBuffer, tmp, 256);
			}
	}


	// set bam header sector
	c1581->goTrackSector(40, 0);
	tmp[0] = 0x28;	// track of first BAM
	tmp[1] = 0x03;	// sector of first BAM
	tmp[2] = 0x44;	// dos version type

	int z=0;
	// disk title
	for(; z<strlen(name); z++)
		tmp[0x04+z] = name[z];

	// filename padding with int 160
	for(; z<16;z++)
		tmp[0x04+z] = 0xa0;

	// disk id
	if (softFormat == false)
	{
		tmp[0x16] = id[0];
		tmp[0x17] = id[1];
	}
	else
	{
		tmp[0x16] = c1581->sectorBuffer[0x16];
		tmp[0x17] = c1581->sectorBuffer[0x17];
	}

	tmp[0x18] = 0xa0;
	tmp[0x19] = 0x33;	// dos version
	tmp[0x1a] = 0x44;	//
	tmp[0x1b] = 0xa0;
	tmp[0x1c] = 0xa0;

	for(z=0x1d; z<= 0xff; z++)
		tmp[z] = 0x00;

	// transfer tmp buffer to the sector
	memcpy(c1581->sectorBuffer, tmp, 256);

	// clear buffer
	for(int t=0;t < 256;t++)
		tmp[t] = 0;

	// BAM side 1
	c1581->goTrackSector(40, 1);
	tmp[0x00] = 0x28;	// next track
	tmp[0x01] = 0x02;	// next sector
	tmp[0x02] = 0x44;	// version #
	tmp[0x03] = 0xbb;	// one compliment above

	// disk id
	if (softFormat == false)
	{
		tmp[0x04] = id[0];
		tmp[0x05] = id[1];
	}
	else
	{
		tmp[0x04] = c1581->sectorBuffer[0x04];
		tmp[0x05] = c1581->sectorBuffer[0x05];
	}

	tmp[0x06] = 0xc0;	// verify/crc flags
	tmp[0x07] = 0x00; 	// autoboot loader flag

	uint8_t tbam[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x24, 0xf0, 0xff, 0xff, 0xff, 0xff
	};

	for(z=0x08;z<=0xff;z++)
		tmp[z] = tbam[z-0x08];

	// transfer tmp buffer to the sector
	memcpy(c1581->sectorBuffer, tmp, 256);

	// BAM side 2
	c1581->goTrackSector(40, 2);
	tmp[0x00] = 0x00;	// next track
	tmp[0x01] = 0xff;	// next sector
	tmp[0xfa] = 0x28;
	tmp[0xfb] = 0xff;

	// transfer tmp buffer to the sector
	memcpy(c1581->sectorBuffer, tmp, 256);

	// dir sector
	c1581->goTrackSector(40, 3);

	// preferably in a soft format, the dir sector
	// isnt cleared.  just the links are cleared
	// but sd2iec does the same thing.
	for(int t=0;t < 256;t++)
		tmp[t] = 0;

	tmp[0] = 0x00;
	tmp[1] = 0xff;

	// transfer tmp buffer to the sector
	memcpy(c1581->sectorBuffer, tmp, 256);

	// rewrite the image
	c1581->write_d81();

	// done
	get_last_error(ERR_OK,0,0);
}

void C1581_CommandChannel :: copy(command_t& command)
{
	char fromName[32];
	int  fromPart;
	char toName[32];
	int  toPart;
	uint8_t destTrack;
	uint8_t destSector;
	uint8_t srcTrack;
	uint8_t srcSector;
	uint8_t srcType;
	uint8_t srcSizeHi;
	uint8_t srcSizeLo;

	if ((!command.names[1].name) || (!command.names[0].name)) {
		get_last_error(ERR_SYNTAX_ERROR_GEN,0,0);
		return;
	}

	strcpy(toName, command.names[0].name);
	toPart = command.names[0].drive;

	// 1581 does doesnt care what the 'to part' is

	strcpy(fromName, command.names[1].name);
	fromPart = command.names[1].drive;

	// but it does care about to from part
	if(fromPart != -1 && fromPart != 0)
	{
		get_last_error(ERR_SYNTAX_ERROR_CMD,0,0);
		return;
	}

	// file types are not allowed (no commas)
	if(command.names[0].separator != ' ' || command.names[2].separator != ' ')
	{
		get_last_error(ERR_SYNTAX_ERROR_GEN,0,0);
		return;
	}

	if (command.names[1].separator != '=')
	{
		get_last_error(ERR_SYNTAX_ERROR_CMD,0,0);
		return;
	}

	if(c1581->disk_state == e_no_disk81)
	{
		get_last_error(ERR_DRIVE_NOT_READY, 40, 0);
		return;
	}

	printf("From: %d:%s To: %d:%s\n", fromPart, fromName, toPart, toName);

	DirectoryEntry *dirEntry = new DirectoryEntry();

	if (c1581->findDirectoryEntry(toName, NULL, dirEntry) > -1)
	{
		get_last_error(ERR_FILE_EXISTS, 0,0);
		return;
	}

	if(c1581->findDirectoryEntry(fromName, NULL, dirEntry) > -1)
	{
		// Create directory entry
		uint8_t ftype = 2;
		srcTrack = dirEntry->first_data_track;
		srcSector = dirEntry->first_data_sector;
		srcType = dirEntry->file_type;
		srcSizeHi = dirEntry->size_hi;
		srcSizeLo = dirEntry->size_lo;

		c1581->createDirectoryEntry((char *)toName, ftype, &destTrack, &destSector);

		uint8_t nxt_srcTrack;
		uint8_t nxt_srcSector;
		int blocks = 0;
		char tmpBuf[256];

		while(true)
		{
			c1581->goTrackSector(srcTrack, srcSector);
			memcpy(tmpBuf, c1581->sectorBuffer, 256);

			nxt_srcTrack = tmpBuf[0];
			nxt_srcSector = tmpBuf[1];

			c1581->goTrackSector(destTrack, destSector);
			memcpy(c1581->sectorBuffer, tmpBuf, 256);
			blocks++;

			if(c1581->sectorBuffer[0] == 0)
				break;
			else
			{
				c1581->findFreeSector(&destTrack, &destSector);
				c1581->setTrackSectorAllocation(destTrack, destSector, true);
				c1581->sectorBuffer[0] = destTrack;
				c1581->sectorBuffer[1] = destSector;
			}

			srcTrack = nxt_srcTrack;
			srcSector = nxt_srcSector;
		}

		c1581->findDirectoryEntry(toName, NULL, dirEntry);
		dirEntry->size_hi = srcSizeHi;
		dirEntry->size_lo = srcSizeLo;
		dirEntry->file_type = srcType;

		c1581->updateDirectoryEntry(toName, NULL, dirEntry);
		c1581->write_d81();
		get_last_error(ERR_OK, 0,0);

	}
	else
	{
		get_last_error(ERR_FILE_NOT_FOUND, 0,0);
	}

	delete [] dirEntry;
	return;
}

void C1581_CommandChannel :: renam(command_t& command)
{
	char fromName[32];
	int  fromPart;
	char toName[32];
	int  toPart;

	if ((!command.names[1].name) || (!command.names[0].name)) {
		get_last_error(ERR_SYNTAX_ERROR_GEN,0,0);
		return;
	}

	strcpy(toName, command.names[0].name);
	toPart = command.names[0].drive;

	// 1581 does doesnt care what the 'to part' is

	strcpy(fromName, command.names[1].name);
	fromPart = command.names[1].drive;

	// but it does care about to from part
	if(fromPart != -1 && fromPart != 0)
	{
		get_last_error(ERR_SYNTAX_ERROR_CMD,0,0);
		return;
	}

	// file types are not allowed (no commas)
	if(command.names[0].separator != ' ' || command.names[2].separator != ' ')
	{
		get_last_error(ERR_SYNTAX_ERROR_GEN,0,0);
		return;
	}

	if (command.names[1].separator != '=')
	{
		get_last_error(ERR_SYNTAX_ERROR_CMD,0,0);
		return;
	}

	if(c1581->disk_state == e_no_disk81)
	{
		get_last_error(ERR_DRIVE_NOT_READY, 40, 0);
		return;
	}


	printf("From: %d:%s To: %d:%s\n", fromPart, fromName, toPart, toName);

	DirectoryEntry *dirEntry = new DirectoryEntry();
	int x = c1581->findDirectoryEntry(toName, NULL, dirEntry);

	if (x > -1)
	{
		get_last_error(ERR_FILE_EXISTS,0,0);
		return;
	}

	x = c1581->findDirectoryEntry(fromName, NULL, dirEntry);

	if(x > -1)
	{
		for(int t=0;t<16;t++)
			dirEntry->filename[t] = 0;

		int t=0;
		for(;t<strlen(toName);t++)
			dirEntry->filename[t] = toName[t];

		for(;t<16;t++)
			dirEntry->filename[t] = 160;

		x = c1581->updateDirectoryEntry(fromName, NULL, dirEntry);
		get_last_error(ERR_OK,0,0);
	}
	else
	{
		get_last_error(ERR_FILE_NOT_FOUND,0,0);
	}

	delete [] dirEntry;
	return;
}

void C1581_CommandChannel :: scratch(command_t& command)
{
    char name[32];
    int  part;

    bool dummy;
    bool keepExtension = false;
    if (!command.names[0].name) {
        get_last_error(ERR_SYNTAX_ERROR_CMD,0,0);
        return;
    }

    const char *fromExt = "???";
    strcpy(name, command.names[0].name);
    part = command.names[0].drive;

    // does the from name have an extension specified?
    if (command.names[1].name) { // yes!
        const char *ext = GetExtension(command.names[1].name[0], dummy);
        if (dummy) {
            fromExt = ext;
        }
    }

    if(c1581->disk_state == e_no_disk81)
	{
		get_last_error(ERR_DRIVE_NOT_READY, 40, 0);
		return;
	}

    DirectoryEntry *dirEntry = new DirectoryEntry();
    int x = c1581->findDirectoryEntry(name, (char *)fromExt, dirEntry);

    if(x > -1)
    {
    	// if partition, delete and deallocate the range of blocks
    	if(dirEntry->file_type == 0x85)
    	{
    		// temporary, so we can return to the proper directory track/sector
			uint8_t dirtrack = c1581->curtrack;
			uint8_t dirsector = c1581->cursector;

			uint8_t ptrk = dirEntry->first_data_track;
			uint8_t psec = dirEntry->first_data_sector;
			int psize = dirEntry->size_hi * 256 + dirEntry->size_lo;

			for(int y=0; y<psize;y++)
			{
				c1581->setTrackSectorAllocation(ptrk, psec, false);

				psec++;
				if(psec > 39)
				{
					psec=0;
					ptrk++;
				}
			}

			// return to the directory track/sector
			c1581->goTrackSector(dirtrack, dirsector);
			c1581->nxttrack = c1581->sectorBuffer[0x00];
			c1581->nxtsector = c1581->sectorBuffer[0x01];
    	}

		// just update the file type to 0, and deallocate the BAM
		dirEntry->file_type = 0x00;
		x = c1581->updateDirectoryEntry(name, (char*)fromExt, dirEntry);

		if (x > -1)
			c1581->setTrackSectorAllocation(dirEntry->first_data_track, dirEntry->first_data_sector, false);

    }

    delete [] dirEntry;

    if (x > -1)
    	get_last_error(ERR_FILES_SCRATCHED, 1,0);
    else
    	get_last_error(ERR_FILES_SCRATCHED, 0,0);
}

void C1581_CommandChannel :: validate(command_t& command)
{
	uint8_t tmp[256];
	int z=0;

	if(c1581->disk_state == e_no_disk81)
	{
		get_last_error(ERR_DRIVE_NOT_READY, 40, 0);
		return;
	}

	// clear a tmp buffer
	for(int t=0;t < 256;t++)
		tmp[t] = 0;

	//
	// Clear the BAM completely to rebuild it
	//

	// BAM side 1
	c1581->goTrackSector(40, 1);
	tmp[0x00] = 0x28;	// next track
	tmp[0x01] = 0x02;	// next sector
	tmp[0x02] = 0x44;	// version #
	tmp[0x03] = 0xbb;	// one compliment above

	// disk id
	tmp[0x04] = c1581->sectorBuffer[0x04];
	tmp[0x05] = c1581->sectorBuffer[0x05];
	tmp[0x06] = 0xc0;	// verify/crc flags
	tmp[0x07] = 0x00; 	// autoboot loader flag

	uint8_t tbam[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff,
			0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x28, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x24, 0xf0, 0xff, 0xff, 0xff, 0xff
	};

	for(z=0x08;z<=0xff;z++)
		tmp[z] = tbam[z-0x08];

	// transfer tmp buffer to the sector
	memcpy(c1581->sectorBuffer, tmp, 256);

	// BAM side 2
	c1581->goTrackSector(40, 2);
	tmp[0x00] = 0x00;	// next track
	tmp[0x01] = 0xff;	// next sector
	tmp[0xfa] = 0x28;
	tmp[0xfb] = 0xff;

	// transfer tmp buffer to the sector
	memcpy(c1581->sectorBuffer, tmp, 256);

	//
	// Read disk directory and allocate the appropriate chain
	//
	int dirptr = 0;
	bool firstcall = true;
	int dirctr = 0;
	bool lastdirsector = false;
	DirectoryEntry *dirEntry = new DirectoryEntry;

	dirptr = c1581->getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);

	while (dirptr != -1)
	{
		// file is not a splat file
		if((dirEntry->file_type & 128) == 128)
		{
			// if partition (CBM), just allocate the space
			if(dirEntry->file_type == 0x85)
			{
				// temporary, so we can return to the proper directory track/sector
				uint8_t dirtrack = c1581->curtrack;
				uint8_t dirsector = c1581->cursector;

				uint8_t ptrk = dirEntry->first_data_track;
				uint8_t psec = dirEntry->first_data_sector;
				int psize = dirEntry->size_hi * 256 + dirEntry->size_lo;

				for(int y=0; y<psize;y++)
				{
					c1581->setTrackSectorAllocation(ptrk, psec, true);

					psec++;
					if(psec > 39)
					{
						psec=0;
						ptrk++;
					}
				}

				// return to the directory track/sector
				c1581->goTrackSector(dirtrack, dirsector);
				c1581->nxttrack = c1581->sectorBuffer[0x00];
				c1581->nxtsector = c1581->sectorBuffer[0x01];
			}
			else
			{
				// otherwise follow the file chain and allocate

				// temporary, so we can return to the proper directory track/sector
				uint8_t dirtrack = c1581->curtrack;
				uint8_t dirsector = c1581->cursector;
				uint8_t dirnxttrack = c1581->nxttrack;
				uint8_t dirnxtsector = c1581->nxtsector;

				uint8_t trk = dirEntry->first_data_track;
				uint8_t sec = dirEntry->first_data_sector;

				c1581->setTrackSectorAllocation(trk, sec, true);

				// follow the file chain and allocate
				while(trk != 0)
				{
					c1581->goTrackSector(trk, sec);
					trk = c1581->sectorBuffer[0];
					sec = c1581->sectorBuffer[1];
					c1581->setTrackSectorAllocation(trk, sec, true);
				}

				// return to the directory track/sector
				c1581->goTrackSector(dirtrack, dirsector);
				c1581->nxttrack = c1581->sectorBuffer[0x00];
				c1581->nxtsector = c1581->sectorBuffer[0x01];
			}
		}
		else
		{
			// file is a splat file.  remove it from directory
			c1581->sectorBuffer[dirptr + 0x02] = 0x00;
		}

		dirptr = c1581->getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);
	}

	delete dirEntry;
	c1581->write_d81();
	return;
}

void C1581_CommandChannel :: u1(command_t& command)
{
	// parse the command into its component strings
	char * pch;
	char cmdbuf[255];
	char chanl[255];
	char drv[255];
	char trk[255];
	char sec[255];
	uint8_t paramctr = 0;
	uint8_t itrk = 0;
	uint8_t isec = 0;
	uint8_t ichanl = 0;

	pch = strtok(command.cmd," ");

	while (pch != NULL)
	{
		if(paramctr == 0)
			strcpy(cmdbuf, pch);
		else if (paramctr == 1)
			strcpy(chanl, pch);
		else if (paramctr == 2)
			strcpy(drv, pch);
		else if (paramctr == 3)
			strcpy(trk, pch);
		else if (paramctr == 4)
			strcpy(sec, pch);

		pch = strtok (NULL, " ");
		paramctr++;
	}

	itrk = atoi(trk);
	isec = atoi(sec);
	ichanl = atoi(chanl);

	if(itrk < 1 || itrk > 80)
	{
		get_last_error(ERR_ILLEGAL_TRACK_SECTOR, itrk, isec);
		return;
	}

	if(isec < 0 || isec > 39)
	{
		get_last_error(ERR_ILLEGAL_TRACK_SECTOR, itrk, isec);
		return;
	}

	if (c1581->channels[ichanl]->channelopen == false)
	{
		get_last_error(ERR_NO_CHANNEL,0,0);
		return;
	}

	c1581->goTrackSector(itrk, isec);
	for(int t=0; t< BLOCK_SIZE;t++)
		c1581->channels[ichanl]->buffer[t] = c1581->sectorBuffer[t];

	c1581->channels[ichanl]->state = e_block;
	c1581->channels[ichanl]->last_byte = BLOCK_SIZE-1;
	c1581->channels[ichanl]->pointer = 0;
	c1581->channels[ichanl]->prefetch = 0;
	c1581->channels[ichanl]->prefetch_max = BLOCK_SIZE-1;

	get_last_error(ERR_OK, 0,0);

	//TODO: there seems to be a bug in block read (U1) where the last 14 bytes are incorrect for any sector.
	// this only seems to impact the U1 command and not normal program loading
}

void C1581_CommandChannel :: u2(command_t& command)
{
	// parse the command into its component strings
	char * pch;
	char cmdbuf[255];
	char chanl[255];
	char drv[255];
	char trk[255];
	char sec[255];
	uint8_t paramctr = 0;
	uint8_t itrk = 0;
	uint8_t isec = 0;
	uint8_t ichanl = 0;

	pch = strtok(command.cmd," ");

	while (pch != NULL)
	{
		if(paramctr == 0)
			strcpy(cmdbuf, pch);
		else if (paramctr == 1)
			strcpy(chanl, pch);
		else if (paramctr == 2)
			strcpy(drv, pch);
		else if (paramctr == 3)
			strcpy(trk, pch);
		else if (paramctr == 4)
			strcpy(sec, pch);

		pch = strtok (NULL, " ");
		paramctr++;
	}

	itrk = atoi(trk);
	isec = atoi(sec);
	ichanl = atoi(chanl);

	if(itrk < 1 || itrk > 80)
	{
		get_last_error(ERR_ILLEGAL_TRACK_SECTOR, itrk, isec);
		return;
	}

	if(isec < 0 || isec > 39)
	{
		get_last_error(ERR_ILLEGAL_TRACK_SECTOR, itrk, isec);
		return;
	}

	if (c1581->channels[ichanl]->channelopen == false)
	{
		get_last_error(ERR_NO_CHANNEL,0,0);
		return;
	}

	c1581->goTrackSector(itrk, isec);

	for(int t=0; t< BLOCK_SIZE;t++)
		c1581->sectorBuffer[t] = c1581->channels[ichanl]->buffer[t];

	c1581->channels[ichanl]->state = e_block;
	c1581->channels[ichanl]->last_byte = BLOCK_SIZE-1;
	c1581->channels[ichanl]->pointer = 0;
	c1581->channels[ichanl]->prefetch = 0;
	c1581->channels[ichanl]->prefetch_max = BLOCK_SIZE-1;

	c1581->write_d81();
	get_last_error(ERR_OK, 0,0);
}

void C1581_CommandChannel :: block_allocate(command_t& command)
{
	// parse the command into its component strings
	char * pch;
	char cmdbuf[255];
	char drv[255];
	char trk[255];
	char sec[255];
	uint8_t paramctr = 0;
	uint8_t itrk = 0;
	uint8_t isec = 0;
	uint8_t ichanl = 0;

	pch = strtok(command.cmd," ");

	while (pch != NULL)
	{
		if(paramctr == 0)
			strcpy(cmdbuf, pch);
		else if (paramctr == 1)
			strcpy(drv, pch);
		else if (paramctr == 2)
			strcpy(trk, pch);
		else if (paramctr == 3)
			strcpy(sec, pch);

		pch = strtok (NULL, " ");
		paramctr++;
	}

	itrk = atoi(trk);
	isec = atoi(sec);

	if(itrk < 1 || itrk > 80)
	{
		get_last_error(ERR_ILLEGAL_TRACK_SECTOR, itrk, isec);
		return;
	}

	if(isec < 0 || isec > 39)
	{
		get_last_error(ERR_ILLEGAL_TRACK_SECTOR, itrk, isec);
		return;
	}

	if (c1581->channels[ichanl]->channelopen == false)
	{
		get_last_error(ERR_NO_CHANNEL,0,0);
		return;
	}

	bool isallocated = c1581->getTrackSectorAllocation(itrk, isec);

	if(isallocated)
	{
		uint8_t newTrk, newSector;
		c1581->findFreeSector(&newTrk, &newSector);
		get_last_error(ERR_NO_BLOCK, newTrk,newSector);
	}
	else
	{
		c1581->setTrackSectorAllocation(itrk, isec, true);
		get_last_error(ERR_OK, 0,0);
	}
}

void C1581_CommandChannel :: block_free(command_t& command)
{
	// parse the command into its component strings
	char * pch;
	char cmdbuf[255];
	char drv[255];
	char trk[255];
	char sec[255];
	uint8_t paramctr = 0;
	uint8_t itrk = 0;
	uint8_t isec = 0;
	uint8_t ichanl = 0;

	pch = strtok(command.cmd," ");

	while (pch != NULL)
	{
		if(paramctr == 0)
			strcpy(cmdbuf, pch);
		else if (paramctr == 1)
			strcpy(drv, pch);
		else if (paramctr == 2)
			strcpy(trk, pch);
		else if (paramctr == 3)
			strcpy(sec, pch);

		pch = strtok (NULL, " ");
		paramctr++;
	}

	itrk = atoi(trk);
	isec = atoi(sec);

	if(itrk < 1 || itrk > 80)
	{
		get_last_error(ERR_ILLEGAL_TRACK_SECTOR, itrk, isec);
		return;
	}

	if(isec < 0 || isec > 39)
	{
		get_last_error(ERR_ILLEGAL_TRACK_SECTOR, itrk, isec);
		return;
	}

	if (c1581->channels[ichanl]->channelopen == false)
	{
		get_last_error(ERR_NO_CHANNEL);
		return;
	}

	c1581->setTrackSectorAllocation(itrk, isec, false);
	get_last_error(ERR_OK, 0,0);

}

void C1581_CommandChannel :: block_pointer(command_t& command)
{
	// parse the command into its component strings
	char * pch;
	char cmdbuf[255];
	char chanl[255];
	char pos[255];
	uint8_t paramctr = 0;
	uint8_t ichanl = 0;
	uint8_t ipos = 0;

	pch = strtok(command.cmd," ");

	while (pch != NULL)
	{
		if(paramctr == 0)
			strcpy(cmdbuf, pch);
		else if (paramctr == 1)
			strcpy(chanl, pch);
		else if (paramctr == 2)
			strcpy(pos, pch);

		pch = strtok (NULL, " ");
		paramctr++;
	}

	ichanl = atoi(chanl);
	ipos = atoi(pos);

	if(ipos < 0)
		ipos = 0;

	ipos = ipos % 256;

	c1581->channels[ichanl]->pointer = ipos;
	c1581->channels[ichanl]->reset_prefetch();

	get_last_error(ERR_OK,0,0);

	//TODO: B-P should cause the buffer pointer to wrap around to zero if the user continues to read
	// past the end of the buffer
}

void C1581_CommandChannel :: create_select_partition(command_t& command)
{
	// parse the command into its component strings
	char * pch;
	char cmdbuf[255];
	uint8_t istarting_track;
	uint8_t istarting_sector;
	uint8_t ilo_numsectors;
	uint8_t ihi_numsectors;
	uint16_t numsectors;
	uint8_t t=0;

	if(c1581->disk_state == e_no_disk81)
	{
		get_last_error(ERR_DRIVE_NOT_READY, 40, 0);
		return;
	}

	if(command.names[0].name == 0)
	{
		// switch partitions
		get_last_error(ERR_PARTITION_OK, 0, 0);
		return;
	}

	for(t=0; t<255;t++)
	{
		cmdbuf[t] = command.names[0].name[t];
		if(command.names[0].name[t] == 0)
		{
			t++;
			break;
		}
	}

	if(t == 255)
	{
		get_last_error(ERR_SYNTAX_ERROR_CMD, 0,0);
		return;
	}

	istarting_track  = command.names[0].name[t++];
	istarting_sector = command.names[0].name[t++];
	ilo_numsectors   = command.names[0].name[t++];
	ihi_numsectors   = command.names[0].name[t++];

	if(command.names[0].name[t++] != ',')
	{
		get_last_error(ERR_SYNTAX_ERROR_CMD, 0,0);
		return;
	}

	if(command.names[0].name[t++] != 'C')
	{
		get_last_error(ERR_SYNTAX_ERROR_CMD, 0,0);
		return;
	}

	if(command.names[0].name[t] != 0)
	{
		get_last_error(ERR_SYNTAX_ERROR_CMD, 0,0);
		return;
	}

	numsectors = (ihi_numsectors * 256) + ilo_numsectors;

	uint8_t trk = istarting_track;
	uint8_t sec = istarting_sector;

	// next, check if the requested sectors are available
	for(uint8_t x=0; x<numsectors;x++)
	{
		bool isallocated = c1581->getTrackSectorAllocation(trk, sec);

		if(isallocated)
		{
			get_last_error(ERR_ILLEGAL_TRACK_SECTOR, trk,sec);
			return;
		}

		sec++;
		if(sec > 39)
		{
			trk++;
			sec = 0;

			if(trk > 80)
			{
				get_last_error(ERR_ILLEGAL_TRACK_SECTOR, trk,sec);
				return;
			}
		}

	}

	trk = istarting_track;
	sec = istarting_sector;

	// next, allocate the sectors
	for(uint8_t x=0; x<numsectors;x++)
	{
		c1581->setTrackSectorAllocation(trk, sec, true);

		sec++;
		if(sec > 39)
		{
			trk++;
			sec = 0;
		}
	}

	//add the directory entry
	c1581->createDirectoryEntry(cmdbuf, 0x85, &istarting_track, &istarting_sector);

	//update the file size

	DirectoryEntry *dirEntry = new DirectoryEntry();
	strcpy((char*)dirEntry->filename, cmdbuf);
	dirEntry->file_type = 0x85;
	dirEntry->first_data_track = istarting_track;
	dirEntry->first_data_sector = istarting_sector;
	dirEntry->size_hi = ihi_numsectors;
	dirEntry->size_lo = ilo_numsectors;

	uint8_t z = strlen((const char*)dirEntry->filename);
	for(; z < 16; z++)
		dirEntry->filename[z] = 0xa0;

	c1581->updateDirectoryEntry(cmdbuf, "CBM", dirEntry);

	delete dirEntry;

	get_last_error(ERR_PARTITION_OK, istarting_track, istarting_sector);
}

void C1581_CommandChannel :: mem_read(command_t& command)
{
	// parse the command into its component strings
	char * pch;
	char cmdbuf[255];
	char clo[255];
	char chi[255];
	char ccount[255];
	uint8_t paramctr = 0;
	uint8_t lo = 0;
	uint8_t hi = 0;
	uint8_t count = 0;

	lo = command.cmd[3];
	hi = command.cmd[4];
	count = command.cmd[5];

	if(count == 0)
		count = 1;

	uint16_t location = hi * 256 + lo;

	if(location == 0xFEA0)
	{
		buffer[0] = 255;
		buffer[1] = 255;
	}

	if(location == 0xA6E9)
	{
		buffer[0] = 0x38;
		buffer[1] = 0xb1;
	}

	if(location == 0xC000)
	{
		buffer[0] = 0xC0;
		buffer[1] = 0x00;
	}

	if(location == 0xe5c6)
	{
		buffer[0] = 0xff;
		buffer[1] = 0xff;
	}

	last_byte = count-1;
	pointer = 0;
	prefetch = 0;
	prefetch_max = count-1;
	state = e_file;

}

void C1581_CommandChannel :: mem_write(command_t& command)
{
}

void C1581_CommandChannel :: mem_exec(command_t& command)
{
	get_last_error(ERR_OK,0,0);
}

void C1581_CommandChannel :: change_devicenum(command_t& command)
{
	if(command.digits > 7 && command.digits < 30)
	{
		c1581->iec_address = command.digits;
		get_last_error(ERR_OK,0,0);
	}
	else
	{
		get_last_error(ERR_SYNTAX_ERROR_CMD,0,0);
	}
}



