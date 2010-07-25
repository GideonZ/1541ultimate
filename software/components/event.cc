#include "event.h"

Fifo<Event> event_queue(32, c_empty_event);

void push_event(t_event_type ev, void *obj, int prm)
{
    Event event(ev, obj, prm);
    event_queue.push(event);
}
