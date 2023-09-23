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

	virtual void line_breakdown(const char *text_buffer, int buffer_size);
    virtual void draw();
    virtual void draw(int line_idx, Line *line);
    virtual int poll(int);
    virtual int handle_key(uint8_t);
};

class HexEditor : public Editor
{
	UserInterface *user_interface;
	void line_breakdown(const char *text_buffer, int buffer_size);
    void draw(int line_idx, Line *line);
public:
    HexEditor(UserInterface *ui, const char *text_buffer, int max_len);
};

#endif
