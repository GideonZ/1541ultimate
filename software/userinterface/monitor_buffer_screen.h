#ifndef MONITOR_BUFFER_SCREEN_H
#define MONITOR_BUFFER_SCREEN_H

#include "screen.h"

class MonitorBufferScreen : public Screen
{
    Screen *target;
    int size_x;
    int size_y;
    int cursor_x;
    int cursor_y;
    int color;
    bool reverse;
    bool allow_scroll;
    bool dirty;
    char *chars;
    char *colors;
    char *reverse_flags;
    char *written_flags;
    char *synced_chars;
    char *synced_colors;
    char *synced_reverse_flags;
    char *synced_flags;

    void clear_cells();
    void scroll_up();

public:
    MonitorBufferScreen(Screen *target, int size_x, int size_y);
    ~MonitorBufferScreen();

    void cursor_visible(int a);
    void set_background(int c);
    void set_color(int c);
    int get_color();
    void reverse_mode(int r);
    void scroll_mode(bool b);
    int get_size_x(void);
    int get_size_y(void);

    void clear();
    void move_cursor(int x, int y);
    int output(char c);
    int output(const char *c);
    void repeat(char c, int rep);
    void output_fixed_length(const char *string, int offset_x, int width);
    void sync(void);
};

#endif