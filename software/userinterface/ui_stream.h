#ifndef UI_STREAM_H
#define UI_STREAM_H

#include "stream.h"
#include "userinterface.h"

class StreamMenu;

class UserInterfaceStream : public UserInterface
{
    Stream *stream;
    StreamMenu *menu;
    virtual ~UserInterfaceStream() { };
public:
    UserInterfaceStream(Stream *s);

    virtual void run();
    virtual int popup(const char *msg, uint8_t flags); // blocking
    virtual int string_box(const char *msg, char *buffer, int maxlen); // blocking
    
    virtual void show_progress(const char *msg, int steps); // not blocking
    virtual void update_progress(const char *msg, int steps); // not blocking
    virtual void hide_progress(void); // not blocking (of course)

    void set_menu(StreamMenu *menu) { this->menu = menu; }
}; 

#endif
