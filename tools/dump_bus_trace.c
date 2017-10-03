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

void dump_trace(FILE *fi, int max)
{
    uint32_t time = 0;
    int   r,i,z;
    
    struct data_entry d;
    struct data_entry prev = { 0x1234, 0x56, 0x00 };

    const char vcd_header[] = "$timescale\n 500 ns\n$end\n\n";
    const char vcd_middle[] = "\n$enddefinitions $end\n\n#0\n$dumpvars\n";

    //   ev_data_c <= sub & task & ev_data;
//    vector_in <= phi2 & rstn & rwn & ba & irqn & nmin & io1n & io2n & data & addr;

    const char *labels[8] = { "io2","io1","nmi", "irq","ba","RWn","rst","phi2" };

    uint8_t   b;
        
    z = 4;

    printf(vcd_header);
    
    printf("$var wire 16 z addr $end\n");
    printf("$var wire 8 y data $end\n");
    for(b=0;b<8;b++) {
        if(*labels[b])
            printf("$var wire 1 %c %s $end\n", 65+b, labels[b]);
    }    
      
    printf(vcd_middle);
    char buffer[32];
    
    for(i=0;i<max;i++) {
        r = fread(&d, z, 1, fi);
        if(r != 1)
            break;

        time ++;
        printf("#%ld\n", time);
    	if (prev.addr != d.addr) {
    		printf("b%s z\n", bin(d.addr, 16, buffer));
    	}
    	if (prev.data != d.data) {
    		printf("b%s y\n", bin(d.data, 8, buffer));
    	}
    	uint8_t change = prev.flags ^ d.flags;
    	for(b=0;b<8;b++) {
            if((change & 1)||(i==0)) {
                printf("%c%c\n", ((d.flags >> b) & 1)+48, 65+b);
            }
            change >>= 1;
        }
        prev = d;
    }
}

int main(int argc, char **argv)
{
    FILE *fi;
    
    if(argc != 2) {
        printf("Usage: dump_vcd <file>\n");
        exit(1);
    }
    fi = fopen(argv[1], "rb");
    if(fi) {
//    	fseek(fi, 0xe6c000, SEEK_SET);
    	fseek(fi, 0x000000, SEEK_SET);
    	dump_trace(fi, 63*2*312*100);
    } else {
        fprintf(stderr, "Can't open file.\n");
        exit(2);
    }
    fclose(fi);
    return 0;
}
