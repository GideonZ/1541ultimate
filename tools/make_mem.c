
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *buffer;
    int i, size, offset;
    
    if((argc != 3)&&(argc != 4)) {
        printf("Usage: make_mem <input file> <size> [offset]\n");
        return 0;
    }
    
    sscanf(argv[2], "%d", &size);

    if (argc == 4) {
        sscanf(argv[3], "%d", &offset);
    } else {
        offset = 0;
    }
    
    buffer = malloc(size);
    memset(buffer, 0xAA, size);

    f = fopen(argv[1], "rb");
    if(!f) {
        printf("Can't open file %s.\n", argv[1]);
        free(buffer);
        return 1;
    }
    
    fread((char *)buffer, 1, size, f);
    fclose(f);    

    for(i=0;i<size;i++) {
        if ((i % 16) == 0)
            printf("\n@%04X ",i + offset);
            
        printf("%02X ", buffer[i]);

    }
    printf("\n");
    free(buffer);
    return 0;
}
