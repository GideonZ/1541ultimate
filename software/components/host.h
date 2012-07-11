#ifndef HOST_H
#define HOST_H

#include "event.h"
#include "integer.h"

class Keyboard;

class GenericHost
{
public:
    GenericHost() { }
    virtual ~GenericHost() { }

    virtual bool exists(void) { return false; }
    virtual bool is_accessible(void) { return false; }
    virtual void poll(Event &e) { }
    virtual void reset(void) { }
    virtual void freeze(void) { }
    virtual void unfreeze(Event &e) { }
    virtual void set_colors(int background, int border) { }

    /* We should actually just return an output device */
    virtual char *get_screen(void) { return NULL; }
    virtual char *get_color_map(void) { return NULL; }
    /* We should actually just return an input device type */
    virtual Keyboard *get_keyboard(void) { return NULL; }
};


#endif
