
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *buffer;
    int i, size, width;
    
    if(argc != 5) {
        printf("Usage: make_array <input file> <size> <name> <width>\n");
        return 0;
    }
    
    size = strtol(argv[2], NULL, 0);
    width = strtol(argv[4], NULL, 0);
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

    printf("library ieee;\nuse ieee.std_logic_1164.all;\n\npackage %s_pkg is\n\n", argv[3]);
    printf("    type t_%s_array is array (natural range <>) of std_logic_vector(7 downto 0);\n\n", argv[3]);
    printf("    constant %s_array : t_%s_array(0 to %d) := (", argv[3], argv[3], size-1);

    for(i=0;i<size;i++) {
        if ((i % width) == 0)
            printf("\n        ");
            
        printf("X\"%02X\"", buffer[i]);

        if (i != (size-1))
            printf(", ");
        else
            printf(" );\n");
           
    }
    printf("\nend;\n");
    free(buffer);
    return 0;
}
