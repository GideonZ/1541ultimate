#ifndef STREAM_MENU_H
#define STREAM_MENU_H

#include <stdlib.h>
#include <string.h>
#include "stream.h"
#include "path.h"
#include "event.h"

// Base class menu, is capable of listing items from a list
// number them, wait for user choice and return it.

class StreamMenu
{
    int state;
    int selected;
    PathObject *node;
    PathObject *menu;
    Stream *stream;
    char user_input[32];

    void print_items(int, int);
public:
    StreamMenu(Stream *s, PathObject *node);
    virtual ~StreamMenu() {}

    virtual int poll(Event &e);
    virtual int process_command(char *cmd);
    virtual int into(void);
    virtual void invalidate(PathObject *obj);
};

#endif
