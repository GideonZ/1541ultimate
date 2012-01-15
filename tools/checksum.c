#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>


int main(int argc, char** argv)
{
	int i;

    int      help       = 0;
    int      unnamed    = 0;
    int      size_set   = 0;
    char     name_in[1024];
    char     name_out[1024];
    
    FILE     *fi, *fo;
    
    if(argc == 1)
        help = 1;
        
    // Parse options
    for(i=1;i<argc;i++) {
        if(argv[i][0] == '-') { // option
            switch(argv[i][1]) {

            case 'h':
                help = 1;
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
        fprintf(stderr, "Usage: %s [-h] <infile> <outfile>\n", argv[0]);
        fprintf(stderr, "\t-h shows this help.\n");
        return 1;
    }

    fi = fopen(name_in, "rb");
    if(!fi) {
        fprintf(stderr, "Can't open '%s' as input.\n", name_in);
        exit(-1);
    }
    fo = fopen(name_out, "w");
    if(!fo) {
        fprintf(stderr, "Can't open '%s' as output.\n", name_out);
        exit(-1);
    }

    int b = 0;
    int check = 0;
    do {
        b = fgetc(fi);
        if (b == EOF)
            break;
        if(check < 0)
            check = check ^ 0x801618D5; // makes it positive! ;)
        check <<= 1;
        check += b;
    } while(b != EOF); 
    char *n = name_in;
    char *fn = n;
    while(*n) {
        if(*n == '\\')
            fn = n+1;
        else if(*n == '/')
            fn = n+1;
        else if(*n == '.')
            *n = '_';
        else if(*n == '-')
            *n = '_';
        n++;
    }
            
    fprintf(fo, "#define CHK_%s (0x%08X)\n", fn, check);


    fclose(fo);
    fclose(fi);

    return 0;
}
