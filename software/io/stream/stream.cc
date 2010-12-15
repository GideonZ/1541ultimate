extern "C" {
    #include "itu.h"
    #include "small_printf.h"
}
#include "stream.h"

int Stream :: read(BYTE *buffer, int length)    
{
    return (int)uart_read_buffer(buffer, (USHORT)length);
}

int Stream :: write(BYTE *buffer, int length)
{
    return (int)uart_write_buffer(buffer, (USHORT)length);
}

int Stream :: getchar(void)
{
	if (UART_FLAGS & UART_RxDataAv)
        return uart_get_byte(0);
    else
        return -1;
}

void Stream :: charout(int c)
{
    if(c == '\n') {
    	while (UART_FLAGS & UART_TxFifoFull);
    	UART_DATA = '\r';
    }
	while (UART_FLAGS & UART_TxFifoFull);
	UART_DATA = c;
}
    
int Stream :: format(char *fmt, ...)
{
    va_list ap;
    int ret;

    write((BYTE *)"\033[1m", 4);

    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);

    write((BYTE *)"\033[0m", 4);

    return (ret);
}


int Stream :: convert_out(int val, char *buf, int radix, char *digits, int leading_zeros, int width)
{
    char temp[80];
    char *cp = temp;
    int length = 0;

    unsigned int v;
	if(val < 0) {
		*buf++ = '-';
		length++;
		v = -val;
	} else {
        v = val;
    }

    if (v == 0) {
        /* Special case */
        *cp++ = '0';
        length++;
    } else {
        while (v) {
            *cp++ = digits[v % radix];
            length++;
            v /= radix;
        }
    }
    if(leading_zeros) {
    	while(length < width) {
    		*cp++ = '0';
			length++;
    	}
    }
    while (cp != temp) {
        *buf++ = *--cp;
    }
    *buf = '\0';
    return (length);
}

int Stream :: convert_in(char *buf, int radix, char *digits)
{
    int pos = strlen(buf);
    int weight = 1;
    int value = 0;
    int num_digits = strlen(digits);
    char c;
    while(pos) {
        c = buf[--pos];
        if(c == '-') {
            value = -value;
            return value;
        } else {
            for(int i=0;i<num_digits;i++) {
                if(digits[i] == c) {
                    value += (i * weight);
                    weight *= radix;
                    break;
                }
            }
        }
    }
    return value;
}

void Stream :: hex(int val, char *buf, int len)
{
    static char hexchar[] = "0123456789ABCDEF";
    if(!len)
        return;
        
    do {
        len--;
        buf[len] = hexchar[val & 15];
        val >>= 4;
    } while(len);
}
    
    
#define is_digit(c) ((c >= '0') && (c <= '9'))

int Stream :: vprintf(char *fmt, va_list ap)
{
    char buf[sizeof(long long)*8];
    char c, sign, *cp=buf;
    int  i;
    long long val = 0;
    int res = 0, length, width;
    int prepad, postpad, leading_zeros;
    int addr;
    
    while ((c = *fmt++) != '\0') {
        if (c == '%') {
            c = *fmt++;
            leading_zeros = (c == '0')?1:0;
            width = 0;
            prepad = 0;
            postpad = 0;
            while((c >= '0')&&(c <= '9')) {
                width = (width * 10) + (int)(c-'0');
                c = *fmt++;
            }

            // Process output
            switch (c) {
            case 'd':
                val = va_arg(ap, int); // up to dword
                length = convert_out(val, buf, 10, "0123456789", leading_zeros, width);
                if(length < width)
                    prepad = width - length;
                cp = buf;
                break;
            case 's':
                cp = va_arg(ap, char *);
                length = 0;
                while (cp[length] != '\0') length++;
                if(length < width)
                    postpad = width - length;
                if((width) && (length > width))
                    length = width; // truncate
                break;
            case 'c':
                c = va_arg(ap, int /*char*/);
                charout(c);
                res++;
                continue;
            case 'p': // pointer
                addr = va_arg(ap, int);
                length = 7;
                hex(addr, buf, length);
                cp = buf;
                break;
            case 'x': // any hex length
                addr = va_arg(ap, int); // up to dword
                hex(addr, buf, width);
                length = width;
                cp = buf;
                break;
            case 'b': // byte
                addr = va_arg(ap, int); // byte
                length = 2;
                hex(addr, buf, length);
                cp = buf;
                break;
            default:
                charout('%');
                charout(c);
                res += 2;
                continue;
            }
            while (prepad-- > 0) {
                charout(' ');
                res++;
            }    
            while (length-- > 0) {
                c = *cp++;
                charout(c);
                res++;
            }
            while (postpad-- > 0) {
                charout(' ');
                res++;
            }    
        } else {
            charout(c);
            res++;
        }
    }
    return (res);
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
    
    int c = getchar();
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
