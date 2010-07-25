
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *buffer;
    int i, size;
    
    if(argc != 3) {
        printf("Usage: make_array <input file> <size>\n");
        return 0;
    }
    
    sscanf(argv[2], "%d", &size);

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

    printf("library ieee;\nuse ieee.std_logic_1164.all;\n\npackage sd_rom_pkg is\n\n");
    printf("    type t_sdrom_array is array (natural range <>) of std_logic_vector(7 downto 0);\n\n");
    printf("    constant sdrom_array : t_sdrom_array(0 to %d) := (", size-1);

    for(i=0;i<size;i++) {
        if ((i % 10) == 0)
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
