#ifndef SCREEN_H
#define SCREEN_H

#define CHR_LOWER_RIGHT_CORNER  0x01
#define CHR_HORIZONTAL_LINE     0x02
#define CHR_LOWER_LEFT_CORNER   0x03
#define CHR_VERTICAL_LINE       0x04
#define CHR_UPPER_RIGHT_CORNER  0x05
#define CHR_UPPER_LEFT_CORNER   0x06
#define CHR_ROUNDED_LOWER_RIGHT 0x07
#define CHR_COLUMN_BAR_TOP      0x08 // not usable!
#define CHR_ROUNDED_LOWER_LEFT  0x09
#define CHR_ROW_LINE_LEFT       0x0A // not usable!
#define CHR_SOLID_BAR_LOWER_7   0x0B
#define CHR_ROW_LINE_RIGHT      0x0C
#define CHR_COLUMN_BAR_BOTTOM   0x0D // not usable!
#define CHR_ROUNDED_UPPER_RIGHT 0x0E
#define CHR_ROUNDED_UPPER_LEFT  0x0F
#define CHR_ALPHA               0x10
#define CHR_BETA                0x11
#define CHR_SOLID_BAR_UPPER_7   0x12
#define CHR_DIAMOND             0x13

#define BORD_LOWER_RIGHT_CORNER  CHR_LOWER_RIGHT_CORNER  
#define BORD_LOWER_LEFT_CORNER   CHR_LOWER_LEFT_CORNER   
#define BORD_UPPER_RIGHT_CORNER  CHR_UPPER_RIGHT_CORNER  
#define BORD_UPPER_LEFT_CORNER   CHR_UPPER_LEFT_CORNER   

class Screen
{
public:
    Screen() { }
    virtual ~Screen() { }

    // functions called directly, or from a window
    virtual void  cursor_visible(int a) { }
    virtual void set_background(int c) { }
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
    virtual void set_status(const char *message, int color);

    // raw
    virtual void output_raw(char c) { }

    // Synchronization
    virtual void sync(void) { }

    // for character mapped screens. Does not work for VT100.
    virtual void backup(void) { }
    virtual void restore(void) { }

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
    int background;
    int reverse;
    int cursor_on;
    bool allow_scroll;
    char *backup_chars;
    char *backup_color;
    int backup_x, backup_y, backup_size;
    void output_raw(char c);
public:
    Screen_MemMappedCharMatrix(char *, char *, int, int);
    ~Screen_MemMappedCharMatrix() { }

    void backup(void);
    void restore(void);
    void update_size(int sx, int sy);

    // functions called directly, or from a window
    void  cursor_visible(int a);
    void  set_background(int c) { background = c; };
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
	int old_color;

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
    virtual void  set_background(int);
    virtual void  reverse_mode(int);
    virtual void  no_scroll(void);

    // virtual int   get_color(void);
    virtual void  clear();
    virtual void  move_cursor(int, int);
    virtual void  output(char);
    virtual void  output(const char *);
    virtual void  output_line(const char *, int indent=0);
    virtual void  output_length(const char *, int);
    virtual void  repeat(char a, int len);
    virtual void  draw_border(void);
    virtual void  reset_border(void);
    virtual void  draw_border_horiz(void);
    virtual void  reset_border_horiz(void);
    virtual int   get_size_x(void);
    virtual int   get_size_y(void);
    virtual void  set_char(int x, int y, char);

    void  dump(void);
};

int console_print(Screen *screen, const char *fmt, ...);

#endif

