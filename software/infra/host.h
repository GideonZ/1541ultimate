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
    // Whether a keyboard scan may drive the machine's CIA right now, from any
    // task: the host holds the bus and is not restoring the program's I/O.
    virtual bool keyboard_scan_allowed(void) { return is_accessible(); }
    // Whether a scan that is allowed should still wait for the next tick: the
    // host is momentarily driving the bus in the program's own banking, so
    // the CIA may not be at its address right now. A deferred scan keeps its
    // key state, unlike a disallowed one; the key is still down.
    virtual bool keyboard_scan_deferred(void) { return false; }
    virtual bool is_permanent(void) { return false; }

    //    virtual void reset(void) { }
    virtual void set_colors(int background, int border) { }

    virtual Screen   *getScreen(void) { return NULL; }
    virtual Keyboard *getKeyboard(void) { return NULL; }

    virtual void checkButton(void) {  }
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
