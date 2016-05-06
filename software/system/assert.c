/*
 * assert.c
 *
 *  Created on: Apr 27, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include <stdint.h>

/*-----------------------------------------------------------*/
void vAssertCalled( char* fileName, uint16_t lineNo )
{
    printf("ASSERTION FAIL: %s:%d\n", fileName, lineNo);
    while(1)
        ;
}


