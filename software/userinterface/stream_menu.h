#ifndef STREAM_MENU_H
#define STREAM_MENU_H

#include <stdlib.h>
#include <string.h>
#include "stream.h"
#include "stack.h"
#include "path.h"
#include "browsable.h"
#include "userinterface.h"

// Base class menu, is capable of listing items from a list
// number them, wait for user choice and return it.

class StreamMenu
{
    int state;
    int selected;
    Browsable *root;
    Browsable *node;
    Stream *stream;
    char user_input[32];
    Path *currentPath;
    UserInterface *user_interface;

    Stack<Browsable *> nodeStack;
    Stack<IndexedList<Browsable *>*> listStack;
    IndexedList<Browsable *>*currentList;
    IndexedList<Action *>actionList;

    void print_items(int, int);
    void print_actions();
    void cleanupBrowsables(IndexedList<Browsable *> &list);
    void cleanupActions();
    void testStack();
public:
    StreamMenu(UserInterface *ui, Stream *s, Browsable *node);
    virtual ~StreamMenu();

    virtual int poll();
    virtual int process_command(char *cmd);
    virtual int into(void);
    //virtual void invalidate(void *obj);
};

#endif
