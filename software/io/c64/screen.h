#ifndef SCREEN_H
#define SCREEN_H

class Screen;

class VT100_Parser // can be used / reused for C64 screen, VDC or other character grid screen
{
    // processing state
	char escape_cmd_data[16];
    bool escape;
    bool escape_cmd;
    int  escape_cmd_pos;

    Screen *owner; // it will call the functions to set color / mode on this object

    void  parseAttr();
    void  doAttr(int);
public:
    VT100_Parser(Screen *owner) : owner(owner) {
    	escape = false;
    	escape_cmd = false;
    	escape_cmd_pos = 0;
    }
    int processChar(char c); // returns 1 when char is printed, 0 otherwise.
};

class Screen
{
	VT100_Parser *term;
	char *char_base;
    char *color_base;

	int size_x;
	int size_y;

    // current position
    int cursor_x;
	int cursor_y;
	int pointer;

	friend class VT100_Parser;
	void output_raw(char c);

	// private stuff
	void scroll_up(void);
    void scroll_down(void);

protected:
    // draw mode
    int color;
    int reverse;
    int cursor_on;
    bool allow_scroll;

public:
    Screen(char *, char *, int, int);
    virtual ~Screen();
    void enableVT100Parsing();

// functions called directly, or from a window
    void  cursor_visible(int a) {
    	if (cursor_on != a) {
    		char *p = char_base + pointer;
    		*p ^= 0x80; // remove or place cursor
    	}
    	cursor_on = a;
    }
    virtual void set_color(int c) {color=c;}
    virtual int  get_color() { return color; }
    virtual void reverse_mode(int r) {reverse=r;}
    virtual void scroll_mode(bool b) {allow_scroll=b;}

    // draw functions
    virtual void move_cursor(int x, int y);
    virtual int  output(char c);
    virtual void repeat(char c, int rep);
    virtual void output_fixed_length(char *string, int offset_x, int width);
};

class Window
{
    Screen *parent;

    // limits
    int window_x;
    int window_y;
    int offset_x;
    int offset_y;
    int border_h;
    int border_v;

    // current position
    int cursor_x;
    int cursor_y;

public:
    Window(Screen*, int, int, int, int);
    virtual ~Window();

    virtual void  set_color(int);
    virtual void  reverse_mode(int);
    virtual void  no_scroll(void);

    // virtual int   get_color(void);
    virtual void  clear();
    virtual void  move_cursor(int, int);
    virtual void  output(char);
    virtual void  output(char *);
    virtual void  output_line(char *);
    virtual void  output_length(char *, int);
    virtual void  repeat(char a, int len);
    virtual void  draw_border(void);
    virtual void  draw_border_horiz(void);
    virtual int   get_size_x(void);
    virtual int   get_size_y(void);
    virtual void  set_char(int x, int y, char);

    void  dump(void);
};

int console_print(Screen *screen, const char *fmt, ...);

#endif

