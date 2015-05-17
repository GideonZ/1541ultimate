#ifndef SCREEN_H
#define SCREEN_H

class Screen
{
    char *root_base;
    char *root_base_col;
    char *window_base;
    char *window_base_col;

    char *backup_chars;
    char *backup_color;
    void backup(void);
    void restore(void);

protected:
    char escape_cmd_data[16];
    Screen *parent;
    bool allow_scroll;
    bool escape;
    bool escape_cmd;
    int  escape_cmd_pos;
    int cursor_on;
    int size_x;
    int size_y;
    int window_x;
    int window_y;
    int offset_x;
    int offset_y;
    int cursor_x;
    int cursor_y;
    int pointer;
    int color;
    int border_h;
    int border_v;
    int reverse;

public:
    Screen(char *, char *, int, int);
    Screen(Screen*, int, int, int, int);
    virtual ~Screen();

    void  cursor_visible(int a) {
    	if (cursor_on != a) {
    		char *p = window_base + pointer;
    		*p ^= 0x80; // remove or place cursor
    	}
    	cursor_on = a;
    }
    virtual void  parseAttr();
    virtual void  doAttr(int);
    virtual void  set_color(int);
    // virtual int   get_color(void);
    virtual void  clear();
    virtual void  move_cursor(int, int);
    virtual void  no_scroll(void);
    virtual void  scroll_up(); // only used privately
    // virtual void  scroll_down();
    virtual void  output(char);
    virtual void  output(char *);
    virtual void  output_line(char *);
    virtual void  output_length(char *, int);
    virtual void  repeat(char a, int len);
    virtual void  draw_border(void);
    virtual void  draw_border_horiz(void);
    virtual void  reverse_mode(int);
    virtual int   get_size_x(void);
    virtual int   get_size_y(void);
    virtual void  set_char(int x, int y, char);

    void  dump(void);
};

int console_print(Screen *screen, const char *fmt, ...);

#endif

