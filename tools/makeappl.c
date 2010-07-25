#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>


int main(int argc, char **argv)
{
	unsigned char buffer[1024];
	unsigned long size;
	unsigned long addr;
	unsigned long key;

    FILE *fi, *fo;
    struct stat mystat;

    int index;
    int tmp;

    if((argc < 4)||(argc & 1)) {
        printf("Usage: makeappl <outfile> <infile> <address> [(<infile> <address>) ...]\n");
        exit(1);
    }

    fo = fopen(argv[1], "wb");
    if(!fo) {
	    printf("Couldn't open file '%s' for reading.\n", argv[1]);
	    exit(2);
	}

    index = 2;

	key = 0x4D474553; // SEGM

    while(index < argc) {
        fi = fopen(argv[index], "rb");
        if(!fi) {
            printf("Couldn't open file '%s' for reading.\n", argv[index]);
            fclose(fo);
            exit(3);
        }
        stat(argv[index], &mystat);
        size = mystat.st_size;
        index++;
        tmp = sscanf(argv[index], "%lx", &addr);
        if(!tmp) {
            printf("No valid start address given for '%s'.\n", argv[index-1]);
            fclose(fo);
            exit(4);
        }
        fwrite(&key,  4, 1, fo);
        fwrite(&size, 4, 1, fo);
        fwrite(&addr, 4, 1, fo);
        do {
            tmp = fread(buffer, 1, 1024, fi);
            fwrite(buffer, 1, tmp, fo);
        } while(tmp);

        fclose(fi);
        index ++;
    }
    fclose(fo);

    return 0;
}
