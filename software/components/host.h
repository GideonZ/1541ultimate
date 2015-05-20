#ifndef HOST_H
#define HOST_H

#include "event.h"
#include "integer.h"
#include "screen.h"
#include "keyboard.h"

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

    virtual Screen   *getScreen(void) { return NULL; }
    virtual void releaseScreen(void) { }
    virtual Keyboard *getKeyboard(void) { return NULL; }
};


#endif
