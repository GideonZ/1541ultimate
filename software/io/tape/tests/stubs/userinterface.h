// Stand-in for the user interface: the popups and the file name box that the tape code uses,
// recorded so that a test can read them back.
#ifndef TAPE_TESTS_USERINTERFACE_H
#define TAPE_TESTS_USERINTERFACE_H
#include <string>
#include <vector>
#include "keyboard.h"

#define BUTTON_OK     0x01
#define BUTTON_CANCEL 0x02
#define BUTTON_YES    0x04
#define BUTTON_NO     0x08

class UserInterface
{
public:
    bool available = true;
    std::string name_to_give;            // what the string box returns
    std::vector<std::string> popups;     // every popup text, in order
    bool is_available() { return available; }
    void appear() { available = true; }
    int popup(const char *msg, int) { popups.push_back(msg); return BUTTON_OK; }
    int string_box(const char *, char *buffer, int maxlen) {
        strncpy(buffer, name_to_give.c_str(), maxlen);
        buffer[maxlen] = 0;
        return (int)name_to_give.size();
    }
    void send_keystroke(int) { }
};
#endif
