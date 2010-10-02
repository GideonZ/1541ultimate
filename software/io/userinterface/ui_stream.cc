#include "ui_stream.h"
#include "stream_menu.h"

UserInterfaceStream :: UserInterfaceStream(Stream *s)
{
    stream = s;
}
    
void UserInterfaceStream :: handle_event(Event &e)
{
    if(menu)
        menu->poll(e);
}

int  UserInterfaceStream :: popup(char *msg, BYTE flags)
{
    stream->format("Popup: %s, options: ", msg);
    if(flags & BUTTON_OK)     stream->format("(O)k  ");
    if(flags & BUTTON_YES)    stream->format("(Y)es  ");
    if(flags & BUTTON_NO)     stream->format("(N)o  ");
    if(flags & BUTTON_ALL)    stream->format("(A)ll  ");
    if(flags & BUTTON_CANCEL) stream->format("(C)ancel  ");
    stream->format("\n");
    
    int c;
    while(1) {
        c = stream->getchar();
        switch(c) {
            case 'o':
            case 'O':
                if(flags & BUTTON_OK)
                    return BUTTON_OK;
                break;
    
            case 'y':
            case 'Y':
                if(flags & BUTTON_YES)
                    return BUTTON_YES;
                break;
    
            case 'n':
            case 'N':
                if(flags & BUTTON_NO)
                    return BUTTON_NO;
                break;
    
            case 'a':
            case 'A':
                if(flags & BUTTON_ALL)
                    return BUTTON_ALL;
                break;
    
            case 'c':
            case 'C':
                if(flags & BUTTON_CANCEL)
                    return BUTTON_CANCEL;
                break;
            default:
                break;
        }
    }
}

int  UserInterfaceStream :: string_box(char *msg, char *buffer, int maxlen)
{
    stream->format("%s\n:");
    int ret;
    while(ret = stream->getstr(buffer, maxlen) < 0)
        ;
//    stream->format("Stringbox result=%d, String=%s\n", ret, buffer);
    return strlen(buffer);
}
