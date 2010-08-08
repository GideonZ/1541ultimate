#include "event.h"
#include "poll.h"
#include "fifo.h"
#include "indexed_list.h"

IndexedList<PollFunction> poll_list(24, 0 );

void nop(Event& e) {}


void main_loop(void)
{
    PollFunction func;
    Event event;
    bool empty;
    do {
    	empty = event_queue.is_empty();
        event = event_queue.head();
		for(int i=0;i<poll_list.get_elements();i++) {
	        func = poll_list[i];
            func(event);
        }
        if(!empty)
            event_queue.pop();
    } while(event.type != e_terminate);
}

void send_nop(void)
{
    PollFunction func;
	Event event = (Event)c_empty_event;
	for(int i=0;i<poll_list.get_elements();i++) {
		func = poll_list[i];
		func(event);
	}
}

#ifdef TEST_LOOP
#include <stdio.h>
#include <stdlib.h>

void print_event(Event &e)
{
    printf("Address of object = %p. Type = %d.\n", e.object, e.type);
}

void generate_event(Event &e)
{
    static int delay = 0;
    if(delay == 5) {
        Event p(e_invalidate, (void *)&generate_event);
        event_queue.push(p);
    }
    if(delay == 10) {
        Event p(e_terminate, (void *)&main_loop);
        event_queue.push(p);
    }
    ++delay;
    if(delay == 15) {
        exit(1);
    }
}

int main()
{
    poll_list.append(&print_event);
    poll_list.append(&generate_event);
    main_loop();    
}
#endif
