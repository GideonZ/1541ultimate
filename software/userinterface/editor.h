#ifndef EDITOR_H
#define EDITOR_H
#include "userinterface.h"

struct Line
{
    const char *buffer;
    int  length;
};

class Editor : public UIObject
{
	UserInterface *user_interface;
	void line_breakdown(const char *text_buffer, int buffer_size);
    void draw();
public:
    int line_length;
    int height;
    int first_line;
	int linecount;
    const char *complete_text;
    int text_length;
    Screen   *screen;
    Window   *window;
    Keyboard *keyb;
	IndexedList<Line> *text;

    // Member functions
    Editor(UserInterface *ui, const char *text_buffer, int max_len);
    virtual ~Editor();

    virtual void init(Screen *win, Keyboard *k);
    virtual void deinit(void);

    virtual int poll(int);
    virtual int handle_key(uint8_t);
};

#endif
