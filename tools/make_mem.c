
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int usage()
{
    printf("Usage: make_mem [-w] <input file> <output file> <size> [offset]\n");
    return 0;
}

int main(int argc, char **argv)
{
    FILE *f;
    FILE *fo;
    unsigned char *buffer;
    char *filename, *filename2;
    int i, size, offset;
    
    int argfail = 0;

    int w = 0;
    int fill = 0;
    int argidx = 1;

    // first optional switch
    if (argc <= argidx) {
        return usage();
    }
    if (argv[argidx][0] == '-') {
        for (i=strlen(argv[argidx])-1; i>0; i--) {
            if (argv[argidx][i] == 'w')
                w = 1;
            else if (argv[argidx][i] == 'f')
                fill = 1;
        }
        argidx++;
    }

    // second optional switch
    if (argc <= argidx) {
        return usage();
    }
    filename = argv[argidx++];
    
    if (argc <= argidx) {
        return usage();
    }
    filename2 = argv[argidx++];

    if (argc <= argidx) {
        return usage();
    }
    sscanf(argv[argidx++], "%d", &size);

    if (argc > argidx) {
        sscanf(argv[argidx++], "%d", &offset);
    } else {
        offset = 0;
    }
    
    buffer = malloc(size);
    memset(buffer, 0xAA, size);

    f = fopen(filename, "rb");
    if(!f) {
        printf("Can't open file %s.\n", filename);
        free(buffer);
        return 1;
    }
    fo = fopen(filename2, "w");
    if(!fo) {
        printf("Can't open file %s.\n", filename2);
        free(buffer);
        return 1;
    }
        
    int read = fread((char *)buffer, 1, size, f);
    fclose(f);    

    if (!fill)
        size = read;

    if (w) {
        for(i=0;i<size;i++) {
            if ((i % 4) == 0)
                fprintf(fo, "\n@%08X ",(i + offset)>>2);
            fprintf(fo, "%02X", buffer[i]);
        }
    } else {
        for(i=0;i<size;i++) {
            if ((i % 16) == 0)
                fprintf(fo, "\n@%08X ",i + offset);
            fprintf(fo, "%02X ", buffer[i]);
        }            
    }
    fprintf(fo, "\n");
    free(buffer);
    fclose(fo);
    return 0;
}
