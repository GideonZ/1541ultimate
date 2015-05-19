
#include "small_printf.h"
#include "stream.h"

    
int Stream :: format(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = _my_vprintf(Stream :: putc, (void **)this, fmt, ap);
    va_end(ap);

    return (ret);
}


// Returns natural value when done, negative when not done
int Stream :: getstr(char *buffer, int length)
{
    if(str_state == 0) {
        char *b = buffer;
        str_index = 0;
        while(*b) {
            charout(*(b++));
            str_index ++;
        }
        str_state = 1;
    }
    
    int c = get_char();
    if(c < 0)
        return -1;
        
    int ret = -1;
    if(c == '\r') {
        ret = str_index;
        str_state = 0; // exit
        charout('\n');
    } else if(c == 8) { // backspace
        if(str_index != 0) {
            str_index--;
            buffer[str_index] = '\0';
            format("\033[D \033[D");
        }
    } else if(str_index < length) {
        buffer[str_index++] = c;
        buffer[str_index] = 0;
        charout(c);
    }        
    return ret;  
}
