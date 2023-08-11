#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <stdint.h>

struct data_entry {
    uint16_t stamp;
    uint16_t data;
};


static void print_binary (uint64_t val, int bits)
{
  int bit;
  int leading = 1;
  while (--bits >= 0)
    {
      bit = ((val & (1LL << bits)) != 0LL);
      if (leading && (bits != 0) && ! bit)
	continue;
      leading = 0;
      fputc ('0' + bit, stdout);
    }
}

void dump_trace(FILE *fi)
{
    uint32_t time = 0;
    uint16_t  prev, current, change;
    int   r,i,z;
    
    struct data_entry d;
        
    const char vcd_header[] = "$timescale\n 500 ns\n$end\n\n";
    const char vcd_middle[] = "\n$enddefinitions $end\n\n#0\n$dumpvars\n";

    //   ev_data_c <= sub & task & ev_data;
    const char *labels[16] = { "irq[0]","irq[1]","irq[2]", "irq[3]","irq[4]","irq[5]","irq[6]","irq[7]",
                               "task1","task2","task3","task4", "sub[0]","sub[1]","sub[2]","sub[3]" };


    uint8_t   b;
        
    z = 4;

    printf(vcd_header);
    
    for(b=0;b<16;b++) {
        if(*labels[b])
            printf("$var wire 1 %c %s $end\n", 65+b, labels[b]);
    }    
      
    printf(vcd_middle);
    
    for(i=0;;i++) {
        r = fread(&d, z, 1, fi);
        if(r != 1)
            break;

        if(i!=0)
            time += (d.stamp & 0x7FFF) + 1;
        printf("#%d\n", time);
        current = d.data;
        change = current ^ prev;
        for(b=0;b<16;b++) {
            if((change & 1)||(i==0)) {
                printf("%c%c\n", ((current >> b) & 1)+48, 65+b);
            }
            change >>= 1;
        }
/*
        if(z == 6) {
            printf("b");
            print_binary((uint64_t)d.aux, 16);
            printf(" @\n");
        }
*/
        prev = current;
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
        dump_trace(fi); 
    } else {
        fprintf(stderr, "Can't open file.\n");
        exit(2);
    }
    fclose(fi);
    return 0;
}
