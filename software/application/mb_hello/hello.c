#include "small_printf.h"
#include "dhrystone.h"

int main()
{
//    const int sizes[10] = { 384, 96, 24, 5, 64, 100, 1000, 10000, 100000, 0 };

    puts("Hello world.");

    for (int i=0;i<1000;i++) {
    	printf("Iteration %d\n", i);
    }
    /*
    puts("Going to do a dhrystone test run..\n");
    for (int i=0;i<10;i++) {
        int j = ITU_MS_TIMER;
        dhrystone();
        int k = ITU_MS_TIMER;
        printf("Drhystone run %d took %d ms\n", i, k-j);
    }
*/
/*
    for (int i=0;i<10;i++) {
        void *p = malloc(sizes[i]);
        printf("Size %d resulted: %p\n", sizes[i], p);
    }
*/
/*
    int a = 1;
    int b = 0;
    for (int i=0;i<32;i++) {
        if (a < b) {
            printf("%8x is less than %8x\n", a, b);
        } else {
            printf("%8x is NOT less than %8x\n", a, b);
        }
        a <<= 1;
    }

    a = 1;
    for (int i=0;i<32;i++) {
        if (a <= b) {
            printf("%8x is less or equal to than %8x\n", a, b);
        } else {
            printf("%8x is NOT less or equal than %8x\n", a, b);
        }
        a <<= 1;
    }
*/    
    
    return 0;
}
