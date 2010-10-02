#ifndef UI_STREAM_H
#define UI_STREAM_H

#include "userinterface.h"
#include "stream.h"

class StreamMenu;

class UserInterfaceStream : public UserInterface
{
    Stream *stream;
    StreamMenu *menu;
public:
    UserInterfaceStream(Stream *s);
    virtual ~UserInterfaceStream() { };

    virtual void handle_event(Event &e);
    virtual int popup(char *msg, BYTE flags); // blocking
    virtual int string_box(char *msg, char *buffer, int maxlen); // blocking
    
    void set_menu(StreamMenu *menu) { this->menu = menu; }
}; 

#endif
