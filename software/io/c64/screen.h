#ifndef SCREEN_H
#define SCREEN_H

class Screen
{
    char *root_base;
    char *root_base_col;
    char *window_base;
    char *window_base_col;
    bool allow_scroll;
    bool escape;
    
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
    int border;
    int reverse;
    
    char *backup_chars;
    char *backup_color;
    void backup(void);
    void restore(void);
public:
    Screen(char *, char *, int, int);
    Screen(Screen*, int, int, int, int);
    ~Screen();
    
    void  set_color(int);
    int   get_color(void);
    void  set_color(int, int, int, int, int, bool stop=false);
    void  clear();
    void  move_cursor(int, int);
    void  no_scroll(void);
    void  scroll_up();
    void  scroll_down();
    void  output(char);
    void  output(char *);
    void  output_line(char *);
    void  output_length(char *, int);
    void  draw_border(void);
    void  reverse_mode(int);
    void  make_reverse(int x, int y, int len);
    int   get_size_x(void);
    int   get_size_y(void);
    char *get_pointer(void);
};

int console_print(Screen *screen, const char *fmt, ...);

#endif

