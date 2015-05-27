#include <stdio.h>
#include "event.h"
#include "poll.h"
#include "fifo.h"
#include "indexed_list.h"
#include "init_function.h"

#ifdef OS
#include "FreeRTOS.h"
#include "task.h"
#endif

#define DEBUG 1

void nop(Event& e) {}

extern "C" void main_loop(void *a)
{
	MainLoop :: run(a);
}

void MainLoop :: run(void *a)
{
	IndexedList<PollFunction> *poll_list = getPollList();

	PollFunction func;
	FunctionCall call;

    Event event;
    bool empty;
    do {
    	empty = event_queue.is_empty();
    	event = event_queue.head();
        if(DEBUG && !empty)
            printf("Event %2d %p %d\n", event.type, event.object, event.param);

        if(!empty && (event.type == e_function_call)) {
        	call = (FunctionCall)event.object;
        	call((void *)event.param);
            event_queue.pop();
        	continue;
        }

        for(int i=0;i<poll_list->get_elements();i++) {
	        func = (*poll_list)[i];
            func(event);
        }
        if(!empty)
            event_queue.pop();
#ifdef OS
        vTaskDelay(1);
#endif
    } while(event.type != e_terminate);
}

void MainLoop :: nop(void)
{
	IndexedList<PollFunction> *poll_list = getPollList();
    PollFunction func;
	Event event = (Event)c_empty_event;
	for(int i=0;i<poll_list->get_elements();i++) {
		func = (*poll_list)[i];
		func(event);
	}
}

