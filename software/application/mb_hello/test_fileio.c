#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

void main_loop(void *);

int main()
{
    puts("Hello world.");

	xTaskCreate( main_loop, "Main Event Loop", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, NULL );

	vTaskDelay(500);

	test_from_cpp();

	vTaskDelay(100000);

//	chdir("SD");
/*
	FILE *fi;
    const char *filename = "checksum.txt";
    int chunks;

    fi = fopen(filename, "rb");
    if(!fi) {
        printf("Can't open '%s' as input.\n", filename);
        exit(-1);
    }

    uint32_t buffer[16];

    chunks = fread(buffer, 4, 16, fi);
    printf("I read %d chunks.\n", chunks);
    for(int i=0;i<chunks;i++) {
    	printf("%08x ", buffer[i]);
    } printf("\n");

    fseek(fi, 4L, SEEK_SET);
    chunks = fread(buffer, 4, 16, fi);
    printf("I read %d chunks.\n", chunks);
    for(int i=0;i<chunks;i++) {
    	printf("%08x ", buffer[i]);
    } printf("\n");
*/
    return 0;
    
}
