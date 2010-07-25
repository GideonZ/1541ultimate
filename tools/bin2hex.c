#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>

int fread_wrapper(uint8_t *buf, int bytes_to_read, FILE *f, int *add_len, uint32_t fsize, char *version)
{
    int bytes_read = 0;
    if(*add_len == 1) {
        *(uint32_t *)buf = (uint32_t)fsize;
        *add_len = 0;
        bytes_to_read -= 16;
        bytes_read = 16;
        buf += 4;
        memset(buf, 0, 12);
        strncpy(buf, version, 11);
        buf += 12;
    } else if(*add_len == 2) {
        *(buf++) = (fsize >> 24);
        *(buf++) = (fsize >> 16);
        *(buf++) = (fsize >> 8);
        *(buf++) = (fsize >> 0);
        *add_len = 0;
        bytes_to_read -= 16;
        bytes_read = 16;
        memset(buf, 0, 12);
        strncpy(buf, version, 11);
        buf += 12;
    }
    bytes_read += fread(buf, 1, bytes_to_read, f);

    return bytes_read;
}

int main(int argc, char** argv)
{
	int i;

    uint32_t bin_offset = 0;
    uint32_t hex_offset = 0;
    uint32_t length     = (1<<30); // 2G max
    int      eof        = 1;
    int      segment    = -1;
    int      append     = 0;
    int      help       = 0;
    int      unnamed    = 0;
    int      add_length = 0;
    char     name_in[1024];
    char     name_out[1024];
    char     version[16];
    unsigned char buffer[64], b1, b2, ch;
    
    FILE     *fi, *fo;
    
    if(argc == 1)
        help = 1;
        
    memset(version, 0, 16);
    
    // Parse options
    for(i=1;i<argc;i++) {
        if(argv[i][0] == '-') { // option
            switch(argv[i][1]) {
            case 'i':
                if(i != (argc-1)) {
                    bin_offset = strtoul(argv[i+1], 0, 0);
                    i++;
                } else {
                    printf("-i option: no offset given.\n");
                    exit(1);
                }
                break;

            case 'h':
                help = 1;
                break;

            case 'o':
                if(i != (argc-1)) {
                    hex_offset = strtoul(argv[i+1], 0, 0);
                    i++;
                } else {
                    printf("-o option: no offset given.\n");
                    exit(1);
                }
                break;

            case 'l':
                if(i != (argc-1)) {
                    length = strtoul(argv[i+1], 0, 0);
                    i++;
                } else {
                    printf("-l option: no length given.\n");
                    exit(1);
                }
                break;

            case 't':
                eof = 0;
                break;

            case 'a':
                append = 1;
                break;
                
            case 'v':
                if(i != (argc-1)) {
                    strncpy(version, argv[i+1], 11);
                    i++;
                } else {
                    printf("-v option: no string given.\n");
                    exit(1);
                }
                break;

            case 'z':
                add_length = 1;
                break;
                
            case 'Z':
                add_length = 2;
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
        fprintf(stderr, "Usage: %s [-i <bin offset>] [-o <hex offset>] [-l <length>] \n\t\t[-h] [-t] [-z/Z] [-a] <infile> <outfile>\n", argv[0]);
        fprintf(stderr, "\t-h shows this help.\n");
        fprintf(stderr, "\t-t omits the end record.\n");
        fprintf(stderr, "\t-z adds file length word in little endian.\n");
        fprintf(stderr, "\t-Z adds file length word in big endian.\n");
        fprintf(stderr, "\t-a appends to the existing hex file.\n");
        return 1;
    }

    fi = fopen(name_in, "rb");
    if(!fi) {
        fprintf(stderr, "Can't open '%s' as input.\n", name_in);
        exit(-1);
    }
    fo = fopen(name_out, (append)?"at":"wt");
    if(!fo) {
        fprintf(stderr, "Can't open '%s' as output.\n", name_out);
        exit(-1);
    }

    long file_size = 0;
    if(add_length) {
        fseek(fi, 0L, SEEK_END);
        file_size = ftell(fi);
    }
    
    fseek(fi, bin_offset, SEEK_SET);

    int bytes_in_segment;
    int bytes_read;
    int bytes_to_read;
    int seg_flag = 0;
    
    while(length && !feof(fi)) {
        bytes_in_segment = 65536 - (hex_offset & 0xFFFF);
        if((bytes_in_segment == 65536)||(segment == -1)) {
            //:020000040000FA
            segment = (hex_offset >> 16);
            b1 = (segment & 0xFF);
            b2 = (segment >> 8) & 0xFF;
            ch = 0xFA - b1 - b2;
            seg_flag = 1;
        }
        bytes_to_read = (bytes_in_segment > 32)?32:bytes_in_segment;
        bytes_read = fread_wrapper((uint8_t *)buffer, bytes_to_read, fi, &add_length, file_size, version);
        if(bytes_read) {
            if(seg_flag) {
                fprintf(fo, ":02000004%04X%02X\n", segment, ch);
                seg_flag = 0;            
            }
            fprintf(fo, ":%02X%04X00", bytes_read, hex_offset & 0xFFFF);
            b1 = (hex_offset & 0xFF);
            b2 = (hex_offset >> 8) & 0xFF;
            ch = 0 + bytes_read + b1 + b2;
            for(i=0;i<bytes_read;i++) {
                fprintf(fo, "%02X", buffer[i]);
                ch += buffer[i];
            }
			ch = ~ch + 1;
            fprintf(fo, "%02X\n", ch);
        }        
        hex_offset += bytes_read;
        length     -= bytes_read;
    }

    if(eof) {
        fprintf(fo, ":00000001FF\n");
    }
    fclose(fo);
    fclose(fi);

    return 0;
}
