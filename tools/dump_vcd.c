#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

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
        
    const char vcd_header[] = "$timescale\n 1 us\n$end\n\n";
    const char vcd_middle[] = "\n$enddefinitions $end\n\n#0\n$dumpvars\n";

/*
        ev_data <= srq_i & atn_i & data_i & clk_i & '1' & atn_o_2 & data_o_2 & clk_o_2 &
                   '1' & atn_o & data_o & clk_o & hw_srq_o & hw_atn_o & hw_data_o & hw_clk_o;
*/
    const char *labels[16] = { "hw_clk_o", "hw_data_o", "hw_atn_o", "hw_srq_o", "clk_o_8", "data_o_8", "atn_o_8", "dummy1", 
                               "clk_o_9", "data_o_9", "atn_o_9", "dummy2", "clk_i", "data_i", "atn_i", "srq_i" };

    uint8_t   b;
        
    z = 4;

    printf(vcd_header);
    
    for(b=0;b<16;b++) {
        if(*labels[b])
            printf("$var wire 1 %c %s $end\n", 65+b, labels[b]);
    }    
    if(z == 6)
        printf("$var wire 16 @ pc_1541[15:0] $end\n");
      
    printf(vcd_middle);
    
    for(i=0;;i++) {
        r = fread(&d, z, 1, fi);
        if(r != 1)
            break;

        if(i!=0)
            time += (d.stamp & 0x7FFF) + 1;
        printf("#%ld\n", time);
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
