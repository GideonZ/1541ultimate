#ifndef EDITOR_H
#define EDITOR_H
#include "userinterface.h"

struct Line
{
    char *buffer;
    int  length;
};

class Editor : public UIObject
{
    void line_breakdown(char *text_buffer);
    void draw();
public:
    int line_length;
    int height;
    int first_line;
    
    Screen   *parent_win;
    Screen   *window;
    Keyboard *keyb;
	IndexedList<Line> *text;

    // Member functions
    Editor(char *text_buffer);
    virtual ~Editor();

    virtual void init(Screen *win, Keyboard *k);
    virtual void deinit(void);

    virtual int poll(int, Event &e);
    virtual int handle_key(char);
};

#endif
