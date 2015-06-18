#ifndef EDITOR_H
#define EDITOR_H
#include "userinterface.h"

struct Line
{
    const char *buffer;
    int  length;
};

class UserInterface;

class Editor : public UIObject
{
	UserInterface *user_interface;
	void line_breakdown(const char *text_buffer);
    void draw();
public:
    int line_length;
    int height;
    int first_line;
	int linecount;
    
    Screen   *screen;
    Window   *window;
    Keyboard *keyb;
	IndexedList<Line> *text;

    // Member functions
    Editor(UserInterface *ui, const char *text_buffer);
    virtual ~Editor();

    virtual void init(Screen *win, Keyboard *k);
    virtual void deinit(void);

    virtual int poll(int, Event &e);
    virtual int handle_key(uint8_t);
};

#endif
