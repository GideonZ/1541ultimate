#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <stdint.h>

struct data_entry {
    uint16_t addr;
    uint8_t data;
    uint8_t flags;
};

struct format {
    int type;
    int offset;
    int lineLength;
    int frameLength;
    int valid;
};

static char *bin(uint64_t val, int bits, char *buffer)
{
  int bit;
  int leading = 1;
  int i = 0;
  while (--bits >= 0)
    {
      bit = ((val & (1LL << bits)) != 0LL);
      if (leading && (bits != 0) && ! bit)
	continue;
      leading = 0;
      buffer[i++] = '0' + bit;
    }
  buffer[i] = 0;
  return buffer;
}

static int find_string(const char *what, const char *in)
{
    int lenW = strlen(what);
    int lenI = strlen(in);
    int found = -1;
    for(int i=0;i<(lenI-lenW);i++) { // i = startpos
        found = i;
        for(int j=0;j<lenW;j++) {
            if(what[j] != in[i+j]) {
                found = -1;
                break;
            }
        }
        if (found != -1)
            break;
    }
    return found;
}

static void find_format(struct data_entry *entries, int num_entries, struct format *format)
{
    format->valid = 0;
    format->type = 0;
    format->offset = 0;
    format->lineLength = 63;
    format->frameLength = 63 * 312;

    // First, find the horizontal offset and line time, by searching for VIC cycles with decreasing address;
    // since those are refresh cycles
    if (num_entries < 30000) {
        return;
    }

    int refreshOffset = -1;
    int repeat = 0;
    for(int i=0; i < 400; i++) {
        if (!(entries[i].flags & 0x80)) { // VIC cycle
            if (((entries[i].addr & 0xFF00) == 0xFF00)) {
                if (entries[i+8].addr == ((entries[i].addr - 4) | 0xFF00)) {
                    if (refreshOffset == -1) {
                        refreshOffset = i;
                    } else {
                        repeat = i - refreshOffset;
                        if ((repeat != 126) && (repeat != 128) && (repeat != 130)) {
                            refreshOffset = -1;
                            repeat = 0;
                        } else {
                            break;
                        }
                    }
                }
            }
        }
    }
    fprintf(stderr, "Offset = %d. Repeat = %d.\n", refreshOffset, repeat);
    if (repeat)
        format->lineLength = repeat / 2;
    switch(repeat) {
    case 126:
        fprintf(stderr, "PAL format detected.\n");
        format->frameLength = 312 * format->lineLength;
        break;
    case 128:
        fprintf(stderr, "NTSC OLD format detected.\n");
        format->frameLength = 263 * format->lineLength;
        break;
    case 130:
        fprintf(stderr, "NTSC format detected.\n");
        format->frameLength = 262 * format->lineLength;
        break;
    default:
        fprintf(stderr, "No valid format found.\n");
        return;
    }

    char badLines[630];
    for (int i=0;i<630;i++) {
        if (entries[refreshOffset+2+i*repeat].flags & 0x10) {
            badLines[i] = '.';
        } else {
            badLines[i] = 'X';
        }
    }
    badLines[629] = 0;
    int lineOffset = find_string("...................................................X.......X.......X.......X.......X", badLines);
    fprintf(stderr, "LineOffset = %d\n", lineOffset);
    // now we know that at line {lineOffset}, line 0 starts, in Y position, and that cycle 1 of the line is 10 (11-1)
    // cycles earlier than refreshOffset. So the offset to line 0, cycle 1 is: lineOffset*repeat - 20.
    if (lineOffset != -1) {
        format->offset = lineOffset*repeat +refreshOffset - 20;
        format->valid = 1;
    }
}

void mask_refresh(struct data_entry *entries, int max, struct format *fmt)
{
    for(int i=fmt->offset; i < max; i += fmt->lineLength*2) {
        entries[i+20].addr = 0x1234;
        entries[i+22].addr = 0x1233;
        entries[i+24].addr = 0x1232;
        entries[i+26].addr = 0x1231;
        entries[i+28].addr = 0x1230;
        entries[i+20].data = 0x00;
        entries[i+22].data = 0x00;
        entries[i+24].data = 0x00;
        entries[i+26].data = 0x00;
        entries[i+28].data = 0x00;
    }
}

void dump_trace(struct data_entry *entries, int size, int lines, int text_mode, int trigger, int skip_frames, int drive_mode)
{
    uint32_t time = 0, drive_time = 0, cpu_time = 0;

    int   r,i,z;
    
    struct format fmt;
    fmt.valid = 0;
    fmt.type = 0;
    fmt.offset = 0;
    fmt.lineLength = 63;
    fmt.frameLength = 63 * 312;

    if (!drive_mode)
        find_format(entries, size, &fmt);
    if (fmt.valid)
        mask_refresh(entries, size, &fmt);
    fmt.offset += fmt.frameLength * skip_frames;
    size -= fmt.frameLength * skip_frames;

    static struct data_entry init1 = { 0x1234, 0x56, 0x00 };
    static struct data_entry init2 = { 0x5678, 0x89, 0x00 };
    struct data_entry *d;
    struct data_entry *prev = &init1;
    struct data_entry *prev_drv = &init2;

    const char vcd_header[] = "$timescale\n %d ns\n$end\n\n";
    const char vcd_middle[] = "\n$enddefinitions $end\n\n#0\n$dumpvars\n";

    //   ev_data_c <= sub & task & ev_data;
    //    vector_in <= phi2 & gamen & exromn & ba & interrupt & rom & io & rwn & data & addr;
    //
    // vector_in <= phi2 & dman & exromn & ba & irqn & rom & nmin & rwn & data & addr;
    // const char *labels[8] = { "RWn","NMIn","ROMn", "IRQn","BA","EXROMn","SYNC","PHI2" };
    const char *labels[8] = { "RWn","NMIn","ROMn", "IRQn","BA","DMAn","FSYNC","PHI2" };
    const char *drive_labels[8] = { "1541_RWn", "1541_IRQn", "1541_BYTEREADY", "1541_SYNC", "1541_CLOCK", "1541_DATA", "1541_ATN", "nul" };
    const char *vic_regs[47] = { "M0X", "M0Y", "M1X", "M1Y", "M2X", "M2Y", "M3X", "M3Y",
            "M4X", "M4Y", "M5X", "M5Y", "M6X", "M6Y", "M7X", "M7Y",
            "MX8th", "D011", "D012", "LPX", "LPY", "SPREN", "D016", "SPEXPY", "MEMPNT", "IRQREG", "IRQEN",
            "SPPRI", "SPMC", "SPEXPX", "SSCOL", "SGCOL", "D020", "D021", "D022", "D023", "D024", "MM0", "MM1",
            "M0COL", "M1COL", "M2COL", "M3COL", "M4COL", "M5COL", "M6COL", "M7COL" };

    uint8_t   b,fla;
        
    if (!text_mode) {
        printf(vcd_header, drive_mode ? 1 : 500);
        printf("$var wire 16 ! addr $end\n");
        printf("$var wire 8 # data $end\n");
        printf("$var wire 9 $ line $end\n");
        printf("$var wire 6 * cycle $end\n");
        for(b=0;b<8;b++) {
            if(*labels[b])
                printf("$var wire 1 %c %s $end\n", 48+b, labels[b]);
        }
        if (drive_mode) {
            printf("$var wire 16 ( 1541_addr $end\n");
            printf("$var wire 8 ) 1541_data $end\n");
            for(b=0;b<7;b++) {
                printf("$var wire 1 %c %s $end\n", 104+b, drive_labels[b]);
            }
        }
        for(b=0;b<47;b++) {
            printf("$var wire 8 %c _%s $end\n", 56+b, vic_regs[b]);
        }
        printf(vcd_middle);
    } else {
        printf("ADDR,DATA,");
        for(b=0;b<8;b++) {
            printf("%s,", labels[b]);
        }
        printf("ADDR,DATA,");
        for(b=0;b<8;b++) {
            printf("%s,", labels[b]);
        }
        printf("Cycle,Line,Cycle\n");
    }
    char buffer[32];
    int cycle;
    int enable = 1;

    cycle = -1;
    size -= fmt.offset;
    d = &entries[fmt.offset];
    if (trigger >= 0) {
        enable = 0;
        fprintf(stderr, "Trigger set to %X\n", trigger);
    }

    for(i=0;i<size;i++) {
        d->flags |= 0x40;
/*
        if ((d->flags & 0x80) == 0) {
            if (d->flags & 0x40) {
                enable = 1;
                cycle = 16028;
            } else {
                cycle ++;
            }
            cycle = cycle % 19656;
        }
*/

        if ((d->flags & 0x80) == 0) {
            cycle ++;
        }
        cycle = cycle % fmt.frameLength;

        if (trigger >= 0) {
            if (((d->flags & 0x80) != 0) && (d->addr == trigger)) {
                trigger = -1;
                enable = 1;
            }
        }

        if (!enable) {
            d++;
            continue;
        }

        if (lines <= 0) {
            break;
        }
        lines--;

        if (drive_mode) {
            if ((d->flags & 0x80) == 0) {
                drive_time += 1000;
                time = drive_time;
            } else { // CPU
                cpu_time += 1014; // PAL assumed for now
                if (cpu_time < time+1) {
                    fprintf(stderr, "Adjusting cpu time from %d to %d (%d)\n", cpu_time, time + 1, time - cpu_time);
                    cpu_time = time + 1;
                }
                time = cpu_time;
            }
        } else {
            time ++;
        }

        if (text_mode) {
            printf("\"%04X\",\"%02X\",", d->addr, d->data);
            for(fla=d->flags,b=0;b<8;b++) {
                printf("%d,", fla & 1);
                fla >>= 1;
            }
            if (d->flags & 0x80) {
                printf("%d,%d\n", cycle / fmt.lineLength, 1+(cycle % fmt.lineLength));
            }
            d++;
        } else {
            printf("#%u\n", time);
            if (((d->flags & 0x80) == 0) && (!drive_mode)) {
                if ((cycle % 63) == 0) {
                    printf("b%s $\n", bin(cycle / 63, 9, buffer));
                }
                printf("b%s *\n", bin(cycle % 63, 6, buffer));
            }

            // VIC Register update!
            if (((d->flags & 0xA1) == 0xA0) && ((d->addr & 0xFC3F) >= 0xD000) && ((d->addr & 0xFC3F) <= 0xD02F)) {
                printf("b%s %c\n", bin(d->data, 8, buffer), 56+(d->addr & 0x3F));
            }

            if ((!drive_mode) || (d->flags & 0x80)) {
                if (prev->addr != d->addr) {
                    printf("b%s !\n", bin(d->addr, 16, buffer));
                }
                if (prev->data != d->data) {
                    printf("b%s #\n", bin(d->data, 8, buffer));
                }
                uint8_t change = prev->flags ^ d->flags;
                for(b=0;b<8;b++) {
                    if((change & 1)||(i==0)) {
                        printf("%c%c\n", ((d->flags >> b) & 1)+48, 48+b);
                    }
                    change >>= 1;
                }
                prev = d;
            } else { // 1541
                if (prev_drv->addr != d->addr) {
                    printf("b%s (\n", bin(d->addr, 16, buffer));
                }
                if (prev_drv->data != d->data) {
                    printf("b%s )\n", bin(d->data, 8, buffer));
                }
                uint8_t change = prev_drv->flags ^ d->flags;
                for(b=0;b<8;b++) {
                    if((change & 1)||(i==0)) {
                        printf("%c%c\n", ((d->flags >> b) & 1)+48, 104+b);
                    }
                    change >>= 1;
                }
                prev_drv = d;
            }
            d++;
        }
    }
}

int main(int argc, char **argv)
{
    FILE *fi;
    
    if(argc < 2) {
        printf("Usage: dump_vcd [-d] [-t frames] [-w trig] <file>\n");
        exit(1);
    }
    int i = 1;
    int text_mode = 0;
    int length = 63*2*312*85;
    int trigger = -1;
    int file_size = 0;
    int skip_frames = 0;
    int temp = 1;
    int drive_mode = 0;

    if (strcmp(argv[i], "-d") == 0) {
        drive_mode = 1; // if this is set, entries with bit 31 cleared are 1541 accesses
        i++;
    }
    if (strcmp(argv[i], "-f") == 0) {
        i++;
        skip_frames = strtol(argv[i], NULL, 10);
        fprintf(stderr, "Skipping %d frames.\n", skip_frames);
        i++;
    }
    if (strcmp(argv[i], "-t") == 0) {
        i++;
        text_mode = 1;
        temp = strtol(argv[i], NULL, 10);
        fprintf(stderr, "Text mode %d frames.\n", temp);
        i++;
        length = 63*2*312*temp;
    }
    if (strcmp(argv[i], "-w") == 0) {
        i++;
        trigger = strtol(argv[i], NULL, 16);
        i++;
    }

    fi = fopen(argv[i], "rb");

    if(fi) {
        fseek(fi, 0, SEEK_END);
        file_size = ftell(fi);
        fseek(fi, 0, SEEK_SET);
        struct data_entry *entries = (struct data_entry *)malloc(file_size);
        int numberOfEntries = fread(entries, 4, file_size / 4, fi);
        fprintf(stderr, "%d number of entries read.\n", numberOfEntries);
        dump_trace(entries, numberOfEntries, length, text_mode, trigger, skip_frames, drive_mode);
        free(entries);
    } else {
        fprintf(stderr, "Can't open file.\n");
        exit(2);
    }
    fclose(fi);
    return 0;
}
