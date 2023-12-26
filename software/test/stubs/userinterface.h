#ifndef USERINTERFACE_H
#define USERINTERFACE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    MENU_NOP = 0,
    MENU_HIDE = -1,
    MENU_EXIT = -2,
} MenuAction_t;

#define BUTTON_OK     0x01
#define BUTTON_YES    0x02
#define BUTTON_NO     0x04
#define BUTTON_ALL    0x08
#define BUTTON_CANCEL 0x10

class UserInterface
{
    int  popup(const char *msg, uint32_t flags, int count, const char **names, const char *keys)
    {
        char buffer[10];
        printf("%s\n", msg);
        printf("Choose one of the following:\n");
        for (int i=0;i<count;i++) {
            if (flags & (1 << i)) {
                printf("- %c: %s\n", keys[i], names[i]);
            }
        }
        do {
            fgets(buffer, 10, stdin);
            for(int i=0;i<count;i++) {
                if(!(flags & (1 << i)))
                    continue;
                if(keys[i] == buffer[0]) {
                    return (1 << i);
                }
            }
            printf("Invalid input!\n");
        } while(1);
        return 0;
    }

public:
    UserInterface(const char *title)
    {
        printf("Welcome to the User Interface '%s'. This is a dummy!\n", title);
    };
    virtual ~UserInterface() { }

    int  popup(const char *msg, uint8_t flags)
    { // blocking 
        const char *c_button_names[] = { " Ok ", " Yes ", " No ", " All ", " Cancel " };
        const char c_button_keys[] = { 'o', 'y', 'n', 'c', 'a' };
        return popup(msg, flags, 5, c_button_names, c_button_keys);
    }

    int  popup(const char *msg, int count, const char **names, const char *keys)
    { // blocking, custom
        return popup(msg, (1 << count)-1, count, names, keys);
    }

    int  string_box(const char *msg, char *buffer, int maxlen)
    {
        printf("%s\n> ", msg);
        fgets(buffer, maxlen, stdin);
        return strlen(buffer);
    } // blocking

    void show_progress(const char *msg, int steps) {} // not blocking
    void update_progress(const char *msg, int steps) {} // not blocking
    void hide_progress(void) {} // not blocking (of course)
};

#endif
