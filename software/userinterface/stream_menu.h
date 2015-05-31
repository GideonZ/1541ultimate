#ifndef STREAM_MENU_H
#define STREAM_MENU_H

#include <stdlib.h>
#include <string.h>
#include "stream.h"
#include "stack.h"
#include "path.h"
#include "event.h"
#include "browsable.h"

// Base class menu, is capable of listing items from a list
// number them, wait for user choice and return it.

class StreamMenu
{
    int state;
    int selected;
    Browsable *node;
    Stream *stream;
    char user_input[32];

    Stack<IndexedList<Browsable *>*> listStack;
    IndexedList<Browsable *>*currentList;
    IndexedList<Action *>actionList;

    void print_items(int, int);
    void print_actions();
    void cleanupBrowsables(IndexedList<Browsable *> &list);
    void cleanupActions();
public:
    StreamMenu(Stream *s, Browsable *node);
    virtual ~StreamMenu();

    virtual int poll(Event &e);
    virtual int process_command(char *cmd);
    virtual int into(void);
    //virtual void invalidate(void *obj);
};

#endif
