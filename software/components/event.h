#ifndef EVENT_H
#define EVENT_H

#include "fifo.h"

typedef enum _event_type {
    e_nop = 0,
    e_cleanup_path_object,
    e_cleanup_block_device,
    e_refresh_browser,
    e_reload_browser,
	e_browse_into,
    e_terminate,
    e_invalidate,
    e_detach_disk,
    e_button_press,
    e_copper_capture,
    e_unfreeze,
    e_cart_mode_change,
    e_dma_load,
    e_object_private_cmd,
    e_path_object_exec_cmd
} t_event_type;

class Event
{
public:
    t_event_type type;
    void *object;
    int param;

    Event() {
        type = e_nop;
        object = 0;
        param = 0;
    }
    
    Event(t_event_type t, void *obj, int prm) {
        type = t;
        object = obj;
        param = prm;
    }
};

extern Fifo<Event> event_queue;

void push_event(t_event_type ev, void *obj=0, int prm=0);

const Event c_empty_event(e_nop, 0, 0);

#endif
