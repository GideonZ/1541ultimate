#ifndef SCREEN_H
#define SCREEN_H

class Screen
{
public:
    Screen() { }
    virtual ~Screen() { }

    // functions called directly, or from a window
    virtual void  cursor_visible(int a) { }
    virtual void set_color(int c) { }
    virtual int  get_color() { return 0; }
    virtual void reverse_mode(int r) { }
    virtual void scroll_mode(bool b) { }
    virtual int   get_size_x(void) { return 0; }
    virtual int   get_size_y(void) { return 0; }

    // draw functions
    virtual void clear() { }
    virtual void move_cursor(int x, int y) { }
    virtual int  output(char c) { return 0; }
    virtual int  output(const char *c) { return 0; }
    virtual void repeat(char c, int rep) { }
    virtual void output_fixed_length(const char *string, int offset_x, int width) { }

    // raw
    virtual void output_raw(char c) { }

    // Synchronization
    virtual void sync(void) { }

    // Static
    static void _put(char c, void **obj) {
    	((Screen *)obj)->output(c);
    }
};

class Screen_MemMappedCharMatrix : public Screen
{
	bool escape;

	char *char_base;
    char *color_base;

	int size_x;
	int size_y;

    // current position
    int cursor_x;
	int cursor_y;
	int pointer;

	// private stuff
	void scroll_up(void);
    void scroll_down(void);

protected:
    // draw mode
    int color;
    int reverse;
    int cursor_on;
    bool allow_scroll;

    void output_raw(char c);
public:
    Screen_MemMappedCharMatrix(char *, char *, int, int);
    ~Screen_MemMappedCharMatrix() { }

// functions called directly, or from a window
    void  cursor_visible(int a);
    void  set_color(int c) {color=c;}
    int   get_color() { return color; }
    void  reverse_mode(int r) {reverse=r;}
    void  scroll_mode(bool b) {allow_scroll=b;}
    int   get_size_x(void) {return size_x;}
    int   get_size_y(void) {return size_y;}

    // draw functions
    void clear();
    void move_cursor(int x, int y);
    int  output(char c);
    int  output(const char *c);
    void repeat(char c, int rep);
    void output_fixed_length(const char *string, int offset_x, int width);
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

    Screen *getScreen() { return parent; }
    void getOffsets(int &ox, int &oy) { ox = offset_x; oy = offset_y; }

    virtual void  set_color(int);
    virtual void  reverse_mode(int);
    virtual void  no_scroll(void);

    // virtual int   get_color(void);
    virtual void  clear();
    virtual void  move_cursor(int, int);
    virtual void  output(char);
    virtual void  output(const char *);
    virtual void  output_line(const char *);
    virtual void  output_length(const char *, int);
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

