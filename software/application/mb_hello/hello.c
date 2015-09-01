#include <stdio.h>
#include "iomap.h"

#define LED_DATA     *((volatile uint8_t *)(LED_BASE + 0x00))
#define LED_DATA_32  *((volatile uint32_t*)(LED_BASE + 0x00))

int main()
{
    puts("Hello world.");

    LED_DATA_32 = 0; // start frame
    LED_DATA_32 = 0; // start frame
    LED_DATA_32 = 0; // start frame
    LED_DATA_32 = 0; // start frame

    LED_DATA_32 = 0xF0000000;
    LED_DATA_32 = 0xF0000000;
    LED_DATA_32 = 0xF0002000;
    LED_DATA_32 = 0xF0000000;

/*
    // blue
    LED_DATA = 0xF0;
    LED_DATA = 0xFF;
    LED_DATA = 0x00;
    LED_DATA = 0x00;

    // green
    LED_DATA = 0xF0;
    LED_DATA = 0x00;
    LED_DATA = 0xFF;
    LED_DATA = 0x00;

    // red
    LED_DATA = 0xF0;
    LED_DATA = 0x00;
    LED_DATA = 0x00;
    LED_DATA = 0xFF;

    // yellow
    LED_DATA = 0xF0;
    LED_DATA = 0x00;
    LED_DATA = 0xFF;
    LED_DATA = 0xFF;

    // purple
    LED_DATA = 0xF0;
    LED_DATA = 0xC0;
    LED_DATA = 0x00;
    LED_DATA = 0xFF;
*/
    uint32_t color;

    for(int i=0;i<32;i++) {
    	color = 0xF0000000;
    	            // blue
    	              // green
    	                // red
    	color |= (31 - i) << 19;
    	color |= i << 11;
    	LED_DATA_32 = color;
    }

    for(int i=0;i<32;i++) {
    	color = 0xFF000000;
    	            // blue
    	              // green
    	                // red
    	color |= (31 - i) << 11;
    	color |= i << 3;
    	LED_DATA_32 = color;
    }

    for(int i=0;i<32;i++) {
    	color = 0xFF000000;
    	            // blue
    	              // green
    	                // red
    	color |= (31 - i) << 3;
    	color |= i << 19;
    	LED_DATA_32 = color;
    }

    for(int i=0;i<32;i++) {
    	color = 0xFF000000;
    	            // blue
    	              // green
    	                // red
    	color |= (31 - i) << 19;
    	LED_DATA_32 = color;
    }

    LED_DATA = 0xE0;
    LED_DATA = 0x80;
    LED_DATA = 0x80;
    LED_DATA = 0x80;

    LED_DATA_32 = 0xE0FFFFFF;


/*    int a, b, c, d;

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
*/


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
