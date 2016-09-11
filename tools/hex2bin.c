#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>

/*
-------------------------------------------------------------------------------
							read_hexuint8_t
							============
  Abstract:

	Functions retrieves a uint8_t from an intel hexfile
	
  Parameters
	
  Return:
	uint8_t:		the retrieved uint8_t
-------------------------------------------------------------------------------
*/
uint8_t read_hexuint8_t(FILE *fp)
{
	static char buf[2];
	static uint32_t bytes_read;
	uint8_t res = 0;

    bytes_read = fread(buf, 1, 2, fp);
	if(bytes_read == 2) {
		if((buf[0] >= '0')&&(buf[0] <= '9')) res = (buf[0] & 0x0F) << 4;
		else if((buf[0] >= 'A')&&(buf[0] <= 'F')) res = (buf[0] - 55) << 4;
		if((buf[1] >= '0')&&(buf[1] <= '9')) res |= (buf[1] & 0x0F);
		else if((buf[1] >= 'A')&&(buf[1] <= 'F')) res |= (buf[1] - 55);
	}
	return res;
}

void write_record(uint32_t load_addr, uint32_t length, uint32_t start_addr, FILE *fp, uint8_t *buffer)
{
    printf("** %08x %08x %08x **\n", load_addr, length, start_addr);
    fwrite(&load_addr, 1, 4, fp);
    fwrite(&length, 1, 4, fp);
    fwrite(&start_addr, 1, 4, fp);
    length = (length + 3) & ~3; // rounding
    fwrite(buffer, 1, length, fp);
} 

/*
-------------------------------------------------------------------------------
							read_hex_file
							=============
  Abstract:

	Programs an intel hex file to the flash
	
-------------------------------------------------------------------------------
*/
int read_hex_file(FILE *fp, uint8_t *buffer, uint32_t offset, uint32_t size, int records, FILE *fpo)
{
	uint16_t  bytes_read;
	char     buf[8];
	uint8_t	 chk;
    uint16_t  d;
	uint8_t  data[64], i, b, r;
	uint32_t  addr = 0;
	uint32_t  addr2 = 0;
	uint32_t  last_addr = 0xBAD;
	uint32_t  high = 0;
	uint8_t  *paddr;
	uint8_t  *paddr2;
        
	paddr = (uint8_t *)&addr;
	paddr2 = (uint8_t *)&addr2;

    addr = 0;
    int section_open = 0;
    uint32_t section_addr = 0;
    
	while(1) {
        // find start of record
    	do {
    	    bytes_read = fread(buf, 1, 1, fp);
    	} while(bytes_read && (buf[0] != ':'));
    	if(!bytes_read) {
    	    printf("No record.\n");
    		return 0;
        }
    
    	i=0;
    	chk = 0;
    //        printf("Record: ");
    	do {
    		d = read_hexuint8_t(fp);
    		chk += d;
    		data[i++] = d;
    //            printf("%02X ", d);
    	} while(i < (data[0] + 5));
    //        printf("\n");
    
    	if(chk) {
    		printf("Checksum error. Got %02X, expected 00.", chk);
            printf("... File position: %ld.\n", ftell(fp));
    	}
    	switch(data[3]) { // type field
    		case 0x01:
    			//printf("End record.\n");
                if (section_open) {
                    printf("Length of section: %08x\n", last_addr - section_addr);                        
                    if(records) {
                        write_record(section_addr, last_addr - section_addr, addr2, fpo, buffer);
                    }
                }
    			return high;
    
    		case 0x02: // x86 extended memory format
    			if(data[0] == 2) {
                    addr = 0;
    				paddr[1] = data[4];
    				paddr[0] = data[5];
                    addr <<= 4;
                    printf("Segmented address %08X (continuation from: %08X)\n", addr, last_addr);
    			} else {
    				fprintf(stderr, "Segmented address with wrong length.\n");
    				return 0;
    			}
                break;
    
    		case 0x04: // x386 extended address format
    			if(data[0] == 2) {
    				paddr[3] = data[4];
    				paddr[2] = data[5];
                    printf("Extended address %08X. (continuation from: %08X)\n", addr, last_addr);
    			} else {
    				fprintf(stderr, "Extended address with wrong length.\n");
    				return 0;
    			}
                break;
    
            case 0x05: // start address
            case 0x03: // start address
                paddr2[0] = data[7];
                paddr2[1] = data[6];
                paddr2[2] = data[5];
                paddr2[3] = data[4];
                printf("Start address: 0x%08x\n", addr2);
                break;
                                
    		case 0x00: // data
    			paddr[0] = data[2];
    			paddr[1] = data[1];

                if(last_addr != addr) {
                    if (section_open) {
                        printf("Length of section: %08x\n", last_addr - section_addr);                        
                        if(records) {
                            write_record(section_addr, last_addr - section_addr, addr2, fpo, buffer);
                        }
                    }
                    printf("Data at new address (new record): 0x%08x\n", addr);
                    last_addr = addr;
                    section_addr = addr;
                    section_open = 1;
                    if (records) {
                        offset = addr;
                    }
                }

                if(addr < offset) {
                    fprintf(stderr, "Address too small (0x%X is below offset..)\n", addr);
                    return 0;
                }
                if(addr - offset >= size) {
                    fprintf(stderr, "Address too big (0x%X is > (size+offset)..)\n", addr);
                    return 0;
                }
    			for(i=0;i<data[0];i++) {
                    // program uint8_t
                    b = data[4+i];
                    buffer[addr - offset] = b;
                    if((addr - offset) > high)
                        high = addr - offset;
                    addr++;
                }
                last_addr = addr;
    			break;
    
    		default:
    			fprintf(stderr, "Unsupported record type %2x.\n", data[3]);
    			return 0;
    	}
    }
    return high; // shouldn't come here
}

int main(int argc, char** argv)
{
	int i;

    uint32_t bin_offset = 0;
    uint32_t size       = (1<<22); // 4M default
    uint32_t highest    = 0;
    int      segment    = -1;
    int      help       = 0;
    int      fill       = 0xFF;
    int      unnamed    = 0;
    int      size_set   = 0;
    char     name_in[1024];
    char     name_out[1024];
    unsigned char *buffer;
    int      records = 0;
    
    FILE     *fi, *fo;
    
    if(argc == 1)
        help = 1;
        
    // Parse options
    for(i=1;i<argc;i++) {
        if(argv[i][0] == '-') { // option
            switch(argv[i][1]) {
            case 'o':
                if(i != (argc-1)) {
                    bin_offset = strtoul(argv[i+1], 0, 0);
                    i++;
                } else {
                    printf("-o option: no offset given.\n");
                    exit(1);
                }
                break;

            case 'h':
                help = 1;
                break;

            case 's':
                if(i != (argc-1)) {
                    size = strtoul(argv[i+1], 0, 0);
                    size_set = 1;
                    i++;
                } else {
                    printf("-s option: no size given.\n");
                    exit(1);
                }
                break;

            case 'f':
                if(i != (argc-1)) {
                    fill = strtoul(argv[i+1], 0, 0);
                    i++;
                } else {
                    printf("-f option: no fill uint8_t given.\n");
                    exit(1);
                }
                break;

            case 'r':
                printf("Enabled generating record structs\n");
                records = 1;
                break;
                
            default:
                printf("Unknown option %s given.\n", argv[i]);
                exit(2);
            }
        } else {
            switch(unnamed) {
            case 0:
                strncpy(name_in, argv[i], 1024);
                break;

            case 1:
                strncpy(name_out, argv[i], 1024);
                break;
            
            default:
                printf("Superfluous argument %s.\n", argv[i]);
                exit(3);
            }
            unnamed++;
        }
    }

    if(help) {
        fprintf(stderr, "Usage: %s [-o <bin offset>] [-s <size>] \n\t\t[-h] <infile> <outfile>\n", argv[0]);
        fprintf(stderr, "\t-h shows this help.\n");
        return 1;
    }

    fi = fopen(name_in, "r");
    if(!fi) {
        fprintf(stderr, "Can't open '%s' as input.\n", name_in);
        exit(-1);
    }
    fo = fopen(name_out, "wb");
    if(!fo) {
        fprintf(stderr, "Can't open '%s' as output.\n", name_out);
        exit(-1);
    }

    buffer = malloc(size+64); // some more, because we only check the beginning of a hex line address
    memset(buffer, (fill & 0xFF), size+64);

    // load hex file here
    highest = read_hex_file(fi, buffer, bin_offset, size, records, fo);
    
    if (!records) {
        if(size_set) {
            fwrite(buffer, 1, size, fo);
        } else {
            fwrite(buffer, 1, highest + 1, fo);
        }
    }
    free(buffer);

    fclose(fo);
    fclose(fi);

    return 0;
}
