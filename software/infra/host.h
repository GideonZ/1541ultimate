#ifndef HOST_H
#define HOST_H

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
    virtual bool is_permanent(void) { return false; }

    //    virtual void reset(void) { }
    virtual void set_colors(int background, int border) { }

    virtual Screen   *getScreen(void) { return NULL; }
    virtual void releaseScreen(void) { }
    virtual Keyboard *getKeyboard(void) { return NULL; }

    virtual bool buttonPush(void) { return false; }
    virtual bool hasButton(void) { return false; }
    virtual void setButtonPushed(void) {  }

    // This is a temporary hack until the unfreeze function is no longer called from CRT
    virtual void unfreeze(void *, int) { }
};

class HostClient
{
public:
	virtual ~HostClient() { }

	virtual void release_host() { }
};

#endif
