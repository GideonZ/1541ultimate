#include "monitor_buffer_screen.h"

#include <string.h>

MonitorBufferScreen :: MonitorBufferScreen(Screen *target_screen, int width, int height)
{
    target = target_screen;
    size_x = width;
    size_y = height;
    cursor_x = 0;
    cursor_y = 0;
    color = 15;
    reverse = false;
    allow_scroll = true;
    dirty = true;
    chars = new char[size_x * size_y];
    colors = new char[size_x * size_y];
    reverse_flags = new char[size_x * size_y];
    written_flags = new char[size_x * size_y];
    synced_chars = new char[size_x * size_y];
    synced_colors = new char[size_x * size_y];
    synced_reverse_flags = new char[size_x * size_y];
    synced_flags = new char[size_x * size_y];
    clear_cells();
    memset(written_flags, 0, size_x * size_y);
    memset(synced_chars, ' ', size_x * size_y);
    memset(synced_colors, 0, size_x * size_y);
    memset(synced_reverse_flags, 0, size_x * size_y);
    memset(synced_flags, 0, size_x * size_y);
}

MonitorBufferScreen :: ~MonitorBufferScreen()
{
    delete[] chars;
    delete[] colors;
    delete[] reverse_flags;
    delete[] written_flags;
    delete[] synced_chars;
    delete[] synced_colors;
    delete[] synced_reverse_flags;
    delete[] synced_flags;
}

void MonitorBufferScreen :: clear_cells()
{
    memset(chars, ' ', size_x * size_y);
    memset(colors, color, size_x * size_y);
    memset(reverse_flags, 0, size_x * size_y);
}

void MonitorBufferScreen :: scroll_up()
{
    if (!allow_scroll || size_y <= 1) {
        return;
    }

    memmove(chars, chars + size_x, size_x * (size_y - 1));
    memmove(colors, colors + size_x, size_x * (size_y - 1));
    memmove(reverse_flags, reverse_flags + size_x, size_x * (size_y - 1));
    memmove(written_flags, written_flags + size_x, size_x * (size_y - 1));
    memset(chars + size_x * (size_y - 1), ' ', size_x);
    memset(colors + size_x * (size_y - 1), color, size_x);
    memset(reverse_flags + size_x * (size_y - 1), 0, size_x);
    memset(written_flags + size_x * (size_y - 1), 0, size_x);
    if (cursor_y > 0) {
        cursor_y--;
    }
}

void MonitorBufferScreen :: cursor_visible(int)
{
}

void MonitorBufferScreen :: set_background(int)
{
}

void MonitorBufferScreen :: set_color(int c)
{
    color = c;
}

int MonitorBufferScreen :: get_color()
{
    return color;
}

void MonitorBufferScreen :: reverse_mode(int r)
{
    reverse = (r != 0);
}

void MonitorBufferScreen :: scroll_mode(bool b)
{
    allow_scroll = b;
}

int MonitorBufferScreen :: get_size_x(void)
{
    return size_x;
}

int MonitorBufferScreen :: get_size_y(void)
{
    return size_y;
}

void MonitorBufferScreen :: clear()
{
    clear_cells();
    memset(written_flags, 0, size_x * size_y);
    cursor_x = 0;
    cursor_y = 0;
    dirty = true;
}

void MonitorBufferScreen :: move_cursor(int x, int y)
{
    if (x < 0) {
        x = 0;
    } else if (x >= size_x) {
        x = size_x - 1;
    }

    if (y < 0) {
        y = 0;
    } else if (y >= size_y) {
        y = size_y - 1;
    }

    cursor_x = x;
    cursor_y = y;
}

int MonitorBufferScreen :: output(char c)
{
    int index;

    switch (c) {
        case '\r':
            cursor_x = 0;
            return 1;
        case '\n':
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= size_y) {
                scroll_up();
                cursor_y = size_y - 1;
            }
            return 1;
        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
            }
            return 1;
        default:
            break;
    }

    if (cursor_x < 0 || cursor_x >= size_x || cursor_y < 0 || cursor_y >= size_y) {
        return 0;
    }

    index = cursor_y * size_x + cursor_x;
    chars[index] = c;
    colors[index] = color;
    reverse_flags[index] = reverse ? 1 : 0;
    written_flags[index] = 1;
    dirty = true;

    cursor_x++;
    if (cursor_x >= size_x) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= size_y) {
            scroll_up();
            cursor_y = size_y - 1;
        }
    }
    return 1;
}

int MonitorBufferScreen :: output(const char *c)
{
    int written = 0;
    while (*c) {
        written += output(*(c++));
    }
    return written;
}

void MonitorBufferScreen :: repeat(char c, int rep)
{
    for (int i = 0; i < rep; i++) {
        output(c);
    }
}

void MonitorBufferScreen :: output_fixed_length(const char *string, int, int width)
{
    while (width > 0 && *string) {
        width -= output(*(string++));
    }
    while (width-- > 0) {
        output(' ');
    }
}

void MonitorBufferScreen :: sync(void)
{
    if (!dirty || !target) {
        return;
    }

        if (target->prefers_full_refresh()) {
            target->clear();
            for (int y = 0; y < size_y; y++) {
                bool current_reverse = false;
                int current_color = -1;

                target->move_cursor(0, y);
                target->reverse_mode(0);
                for (int x = 0; x < size_x; x++) {
                    int index = y * size_x + x;
                    bool cell_reverse = reverse_flags[index] != 0;
                    int cell_color = (uint8_t)colors[index];
                    if (cell_color != current_color) {
                        current_color = cell_color;
                        target->set_color(cell_color);
                    }
                    if (cell_reverse != current_reverse) {
                        current_reverse = cell_reverse;
                        target->reverse_mode(current_reverse ? 1 : 0);
                    }
                    target->output(chars[index]);
                }
            }
            target->reverse_mode(0);
            target->move_cursor(cursor_x, cursor_y);
            target->sync();
            memset(written_flags, 0, size_x * size_y);
            dirty = false;
            return;
        }

    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            int index = y * size_x + x;
            bool should_render = (written_flags[index] != 0) || (synced_flags[index] != 0);
            char desired_char;
            char desired_color;
            char desired_reverse;

            if (!should_render) {
                continue;
            }

            desired_char = (written_flags[index] != 0) ? chars[index] : ' ';
            desired_color = (written_flags[index] != 0) ? colors[index] : 0;
            desired_reverse = (written_flags[index] != 0) ? reverse_flags[index] : 0;

            if ((synced_flags[index] != 0) && (synced_chars[index] == desired_char) &&
                    (synced_colors[index] == desired_color) && (synced_reverse_flags[index] == desired_reverse)) {
                continue;
            }

            target->move_cursor(x, y);
            target->set_color((uint8_t)desired_color);
            target->reverse_mode(desired_reverse ? 1 : 0);
            target->output(desired_char);

            synced_chars[index] = desired_char;
            synced_colors[index] = desired_color;
            synced_reverse_flags[index] = desired_reverse;
            synced_flags[index] = (written_flags[index] != 0) ? 1 : 0;
        }
    }
    target->reverse_mode(0);
    target->move_cursor(cursor_x, cursor_y);
    target->sync();
    memset(written_flags, 0, size_x * size_y);
    dirty = false;
}