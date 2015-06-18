#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern "C" void outbyte(int);

void _diag_write_char(char c, void **param);
void _string_write_char(char c, void **param);

static const char hexchar[] = "0123456789ABCDEF";

static int
_cvt(int val, char *buf, int radix, const char *digits, int leading_zeros, int width)
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

static void
_hex(int val, char *buf, int len)
{
    if(!len)
        return;
        
    do {
        len--;
        buf[len] = hexchar[val & 15];
        val >>= 4;
    } while(len);
}
    
    
#define is_digit(c) ((c >= '0') && (c <= '9'))


extern "C" int
_my_vprintf(void (*putc)(char c, void **param), void **param, const char *fmt, va_list ap)
{
    char buf[128];
    char c, *cp=buf;
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
            if (c == '#') {
            	width = va_arg(ap, int); // take width parameter from stack
				c = *fmt++;
            } else {
				while((c >= '0')&&(c <= '9')) {
					width = (width * 10) + (int)(c-'0');
					c = *fmt++;
				}
            }
            // Process output
            switch (c) {
            case 'd':
            case 'i':
                val = va_arg(ap, int); // up to dword
                length = _cvt(val, buf, 10, hexchar, leading_zeros, width);
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
                (*putc)(c, param);
                res++;
                continue;
            case 'p': // pointer
                addr = va_arg(ap, int);
                length = 8;
                _hex(addr, buf, length);
                cp = buf;
                break;
            case 'x': // any hex length
            case 'X': // any hex length
                addr = va_arg(ap, int); // up to dword
                _hex(addr, buf, width);
                length = width;
                cp = buf;
                break;
            case 'b': // byte
                addr = va_arg(ap, int); // byte
                length = 2;
                _hex(addr, buf, length);
                cp = buf;
                break;
            default:
                (*putc)('%', param);
                (*putc)(c, param);
                res += 2;
                continue;
            }
            while (prepad-- > 0) {
                (*putc)(' ', param);
                res++;
            }    
            while (length-- > 0) {
                c = *cp++;
                (*putc)(c, param);
                res++;
            }
            while (postpad-- > 0) {
                (*putc)(' ', param);
                res++;
            }    
        } else {
            (*putc)(c, param);
            res++;
        }
    }
    return (res);
}

// Default wrapper function used by diag_printf
void
_diag_write_char(char c, void **param)
{
	if (c=='\n')
	{
		outbyte('\r');
	}
	outbyte(c);
}

void
_string_write_char(char c, void **param)
{
	char **pnt = (char **)param;
	**pnt = c;
	(*pnt)++;
}

extern "C" int printf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = _my_vprintf(_diag_write_char, (void **)0, fmt, ap);
    va_end(ap);
    return (ret);
}

extern "C" int vsprintf(char *dest, const char *fmt, va_list ap)
{
    int ret = _my_vprintf(_string_write_char, (void **)&dest, fmt, ap);
    _string_write_char(0, (void **)&dest);
    return ret;
}

extern "C" int sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    int ret;
	char *pnt = str;
	
    va_start(ap, fmt);
    ret = _my_vprintf(_string_write_char, (void **)&pnt, fmt, ap);
    _string_write_char(0, (void **)&pnt);
    va_end(ap);
    return (ret);
}

int _conv(const char *buf, int pos, int radix, int *result)
{
	*result = 0;
	char c;
	int nega = 0;
	if (buf[pos] == '-') {
		nega = 1;
		pos++;
	}

	while(buf[pos]) {
		c = buf[pos++];
		if (isdigit(c)) {
			*result *= radix;
			*result += ((int)c) - 48;
		} else if ( (radix > 10) && (((c >= 'A') && (c <= 'F')) || ((c >= 'a') && (c <= 'f')))) {
			*result *= radix;
			*result += ((int)(c & 0x0F)) + 9;
		} else if ((c == ' ') || (c == '\n') || (c == '\r') || (c == '\t')) {
			continue;
		} else {
			break;
		}
	}
	if (nega)
		*result = -(*result);

	return pos;
}

extern "C" int _vscanf(const char *buf, const char *fmt, va_list ap)
{
	int do_conv = 0;
	int pos = 0;
	int result = 0;
	char *dest;
	int count = 0;

	int len = strlen(fmt);
	int i;
	for (i=0;i<len;i++) {
		if (fmt[i] == '%') {
			do_conv = 1;
			continue;
		}
		if (do_conv) {
			do_conv = 0;
			void *pntr = va_arg(ap, void *); // get pointer parameter to store result
			switch(fmt[i]) {
			case 'd': // scan for integer
				pos = _conv(buf, pos, 10, &result);
				*((int *)pntr) = result;
				count++;
				break;
			case 'x': // scan for integer from hex
				pos = _conv(buf, pos, 16, &result);
				*((int *)pntr) = result;
				count++;
				break;
			case 's': // up to white space
				dest = (char *)pntr;
				while(buf[pos]) {
					if ((buf[pos] == ' ') || (buf[pos] == '\n') || (buf[pos] == '\r') || (buf[pos] == '\t')) {
						pos ++;
						break;
					}
					*(dest++) = buf[pos++];
				}
				*(dest) = 0;
				count++;
				break;
			default:
				*((int *)pntr) = 0;
			}
			continue;
		}
	}
	return count;
}

extern "C" int sscanf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = _vscanf(buf, fmt, ap);
    va_end(ap);
    return (ret);
}


extern "C" int puts(const char *str)
{
	int i = 0;
	while (*str) {
        outbyte(*(str++));
        i++;
	}
    outbyte('\r');
    outbyte('\n');
    return i+1;
}        

extern "C" int putchar(int a)
{
	_diag_write_char((char)a, 0);
	return a;
}
