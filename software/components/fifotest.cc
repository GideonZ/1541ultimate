#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION
#include "fifo.h"

#include <stdio.h>

int main()
{
    Fifo<int> my_fifo(3, -1);
    
    for(int i=20;i<40;i++) {
        my_fifo.push(i);
    }
    for(int i=0;i<5;i++) {
        printf("%d\n", my_fifo.pop());
    }
    for(int i=50;i<60;i++) {
        my_fifo.push(i);
    }
    printf("Head = %d.\n", my_fifo.head());
    for(int i=0;i<30;i++) {
        printf("%d\n", my_fifo.pop());
    }
}
