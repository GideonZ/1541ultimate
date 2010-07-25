#include <stdarg.h>

extern void outbyte(int);

static int
_cvt(int val, char *buf, int radix, char *digits, int leading_zeros, int width)
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


int
_vprintf(void (*putc)(char c, void **param), void **param, const char *fmt, va_list ap)
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
                length = _cvt(val, buf, 10, "0123456789", leading_zeros, width);
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
                length = 7;
                _hex(addr, buf, length);
                cp = buf;
                break;
            case 'x': // any hex length
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

int
small_printf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = _vprintf(_diag_write_char, (void **)0, fmt, ap);
    va_end(ap);
    return (ret);
}

int
small_sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    int ret;
	char *pnt = str;
	
    va_start(ap, fmt);
    ret = _vprintf(_string_write_char, (void **)&pnt, fmt, ap);
    _string_write_char(0, (void **)&pnt);
    va_end(ap);
    return (ret);
}
