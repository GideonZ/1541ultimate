#include <stdio.h>
#include <string.h>

#include "editor.h"
#include "screen.h"

void outbyte(int b)
{
    char c = (char)b;
    fwrite(&c, 1, 1, stdout);
}

int UserInterface :: keymapper(int c, keymap_options_t)
{
    return c;
}

class CaptureScreen : public Screen
{
    int color;
public:
    CaptureScreen() : color(0) { }

    void set_background(int) { }
    void set_color(int c) { color = c; }
    int get_color() { return color; }
    void reverse_mode(int) { }
};

class CaptureWindow : public Window
{
    int width;
    int position;
public:
    char rendered[128];

    CaptureWindow(Screen *screen, int width) : Window(screen, 0, 0, width, 1), width(width), position(0)
    {
        rendered[0] = 0;
    }

    void output_length(const char *string, int len)
    {
        memcpy(rendered + position, string, len);
        position += len;
        rendered[position] = 0;
    }

    void repeat(char c, int len)
    {
        memset(rendered + position, c, len);
        position += len;
        rendered[position] = 0;
    }

    int get_size_x(void)
    {
        return width;
    }

    void clear_capture()
    {
        position = 0;
        rendered[0] = 0;
    }
};

static int fail(const char *message)
{
    puts(message);
    return 1;
}

int main()
{
    const char bytes[] = { 0x41, 0x00, 0x7a, 0x1f, 0x20, 0x30, 0x31, 0x32, 0x33, 0x34 };
    Editor *editor = new HexEditor(NULL, bytes, sizeof(bytes));

    editor->line_breakdown(bytes, sizeof(bytes));

    if (editor->linecount != 2) {
        return fail("Expected 2 hex rows.");
    }

    Line first = (*editor->text)[0];
    Line second = (*editor->text)[1];

    if ((first.length != 8) || (second.length != 2)) {
        return fail("Unexpected hex row lengths.");
    }

    if ((first.buffer != bytes) || (second.buffer != bytes + 8)) {
        return fail("Unexpected hex row offsets.");
    }

    CaptureScreen screen;
    CaptureWindow window(&screen, CHARS_PER_HEX_ROW);
    editor->window = &window;

    window.clear_capture();
    editor->draw(0, &first);

    if (strcmp(window.rendered, "0000 41 00 7A 1F 20 30 31 32 A.z. 012") != 0) {
        puts("Unexpected rendered first row:");
        puts(window.rendered);
        delete editor;
        return 1;
    }

    delete editor;
    puts("hex_editor_test: OK");
    return 0;
}