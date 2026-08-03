#ifndef USERINTERFACE_H
#define USERINTERFACE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Values must match software/userinterface/userinterface.h.
#define MENU_NOP     0  // Stay in current window
#define MENU_CLOSE  -1  // Window operation is complete and can be closed
#define MENU_HIDE   -2  // The selected action requests the menu to hide.
#define MENU_EXIT   -3  // The selected action requests the menu to exit and be destroyed.

typedef int MenuAction_t;

#define BUTTON_OK     0x01
#define BUTTON_YES    0x02
#define BUTTON_NO     0x04
#define BUTTON_ALL    0x08
#define BUTTON_CANCEL 0x10

class UserInterface
{
    int  popup(const char *msg, uint32_t flags, int count, const char **names, const char *keys)
    {
        // Automated tests must never block on stdin, so the answer is scripted
        // instead of read. It defaults to the first button that is enabled.
        (void)msg;
        (void)names;
        (void)keys;
        if (scripted_answer()) {
            return scripted_answer();
        }
        for (int i = 0; i < count; i++) {
            if (flags & (1 << i)) {
                return (1 << i);
            }
        }
        return 0;
    }

public:
    // Tests set this to force a specific popup answer (a BUTTON_* value).
    static int& scripted_answer()
    {
        static int answer = 0;
        return answer;
    }

    UserInterface(const char *title)
    {
        printf("Welcome to the User Interface '%s'. This is a dummy!\n", title);
    };
    virtual ~UserInterface() { }

    int  popup(const char *msg, uint8_t flags)
    { // blocking 
        const char *c_button_names[] = { " Ok ", " Yes ", " No ", " All ", " Cancel " };
        const char c_button_keys[] = { 'o', 'y', 'n', 'a', 'c' };
        return popup(msg, flags, 5, c_button_names, c_button_keys);
    }

    int  popup(const char *msg, int count, const char **names, const char *keys)
    { // blocking, custom
        return popup(msg, (1 << count)-1, count, names, keys);
    }

    int  string_box(const char *msg, char *buffer, int maxlen)
    {
        (void)msg;
        if (buffer && (maxlen > 0)) {
            buffer[0] = 0;
        }
        return 0;
    }

    void show_progress(const char *msg, int steps) {} // not blocking
    void update_progress(const char *msg, int steps) {} // not blocking
    void hide_progress(void) {} // not blocking (of course)
};

#endif
