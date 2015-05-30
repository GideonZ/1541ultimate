#include "small_printf.h"
#include "dhrystone.h"

int main()
{
    puts("Hello world.");

    int a, b, c, d;

    a=b=c=d=999;
    printf("&a = %p\n", &a);
    printf("&b = %p\n", &b);
    printf("&c = %p\n", &c);
    printf("&d = %p\n", &d);

    int r = sscanf(" 192, 168,-5,106", "%d,%d,%d,%d", &a, &b, &c, &d);

    printf("a = %d\n", a);
    printf("b = %d\n", b);
    printf("c = %d\n", c);
    printf("d = %d\n", d);
    printf("r = %d\n", r);

/*
    puts("Going to do a dhrystone test run..\n");
    for (int i=0;i<10;i++) {
        int j = getMsTimer();
        dhrystone();
        int k = getMsTimer();
        printf("Drhystone run %d took %d ms\n", i, k-j);
    }
*/
    
    return 0;
}
