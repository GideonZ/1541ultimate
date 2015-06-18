#ifndef HOST_H
#define HOST_H

#include "event.h"
#include "integer.h"
#include "screen.h"
#include "keyboard.h"

class HostClient;

class GenericHost
{
public:
    GenericHost() { }
    virtual ~GenericHost() { }

    virtual void take_ownership(HostClient *) { }
    virtual void release_ownership() { }

    virtual bool exists(void) { return false; }
    virtual bool is_accessible(void) { return false; }
    virtual void poll(Event &e) { }
    virtual void reset(void) { }
    virtual void set_colors(int background, int border) { }

    virtual Screen   *getScreen(void) { return NULL; }
    virtual void releaseScreen(void) { }
    virtual Keyboard *getKeyboard(void) { return NULL; }
};

class HostClient
{
public:
	virtual ~HostClient() { }

	virtual void release_host() { }
};

#endif
