#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern "C" void outbyte(int);

void _diag_write_char(char c, void **param);
void _string_write_char(char c, void **param);

static const char hexchar[] = "0123456789ABCDEF";

static int
_cvt(int val, char *buf, int radix, const char *digits, int leading_zeros, int width, bool signd)
{
    char temp[16];
    char *cp = temp;
    int length = 0;

    unsigned int v;
	if((signd) && (val < 0)) {
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
    
static void
_bin(int val, char *buf, int len)
{
    if(!len)
        return;

    do {
        len--;
        buf[len] = (val & 0x01) ? '*' : '.';
        val >>= 1;
    } while(len);
}

#define is_digit(c) ((c >= '0') && (c <= '9'))


extern "C" int
_my_vnprintf(void (*putc)(char c, void **param), void **param, size_t maxlen, const char *fmt, va_list ap)
{
    char buf[32];
    char c;
    const char *cp=buf;
    long long val = 0;
    int res = 0, length, width;
    int prepad, postpad, leading_zeros;
    int addr;
    int prec = 3;
#ifdef FP_SUPPORT
    float fval, rem;
#endif
    while (((c = *fmt++) != '\0') && (res < maxlen)) {
        if (c == '%') {
            c = *fmt++;
            leading_zeros = (c == '0')?1:0;
            width = 0;
            prepad = 0;
            postpad = 0;
            // bool _nega = false;
            if (c == '-') {
            	// _nega = true;
            	c = *fmt++;
            }
#ifdef FP_SUPPORT
            if (c == '.') { // only very simple FP support
            	c = *fmt++;
                prec = (int)(c-'0');
                c = *fmt++;
            }
#endif
            if ((c == '#') || (c == '*')) { // I thought I was smart to invent the # for variable width... * already existed for this! haha!
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
            case 'u':
            case 'i':
                val = va_arg(ap, int); // up to dword
                length = _cvt(val, buf, 10, hexchar, leading_zeros, width, (c != 'u'));
                if(length < width)
                    prepad = width - length;
                cp = buf;
                break;
#ifdef FP_SUPPORT
            case 'f': // simple FP support
                fval = (float)va_arg(ap, double);
                val = (int)fval;
                rem = fval - val;
                for(int i=0;i<prec;i++)
                    rem *= 10.0f;
                length  = _cvt(val, buf, 10, hexchar, leading_zeros, width-prec-1, true);
                buf[length++] = '.';
                if (rem < 0.0f) {
                    rem = -rem;
                }
                length += _cvt(int(rem+0.5), buf+length, 10, hexchar, true, prec, true);
                if(length < width)
                    prepad = width - length;
                cp = buf;
                break;
#endif
            case 's':
                cp = va_arg(ap, char *);
                length = 0;
                if (!cp) {
                    cp = "(null)";
                };
                while (cp[length] != '\0') length++;
                if(length < width)
                    postpad = width - length;
                if((width) && (length > width))
                    length = width; // truncate
                break;
            case 'c':
                if (res < maxlen) {
                    c = va_arg(ap, int /*char*/);
                    (*putc)(c, param);
                    res++;
                }
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
                length = width;
                if(!length || length > 8)
                    length = 8;
                _hex(addr, buf, length);
                cp = buf;
                break;
            case 'b': // byte
                addr = va_arg(ap, int); // byte
                length = 2;
                _hex(addr, buf, length);
                cp = buf;
                break;
            case 'B': // bits of a byte
                addr = va_arg(ap, int); // byte
                length = 8;
                _bin(addr, buf, length);
                cp = buf;
                break;
            default:
                if (res < maxlen-1) {
                    (*putc)('%', param);
                    (*putc)(c, param);
                    res += 2;
                }
                continue;
            }
            while ((prepad-- > 0) && (res < maxlen)) {
                (*putc)(' ', param);
                res++;
            }    
            while ((length-- > 0) && (res < maxlen)) {
                c = *cp++;
                (*putc)(c, param);
                res++;
            }
            while ((postpad-- > 0) && (res < maxlen)) {
                (*putc)(' ', param);
                res++;
            }    
        } else if (res < maxlen) {
            (*putc)(c, param);
            res++;
        }
    }
    return (res);
}

extern "C" int
_my_vprintf(void (*putc)(char c, void **param), void **param, const char *fmt, va_list ap)
{
    return _my_vnprintf(putc, param, 9999, fmt, ap);
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

extern "C" int snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    int ret;
	char *pnt = str;
	
    va_start(ap, fmt);
    /* size==0: write nothing (size-1 would underflow to a huge maxlen and the
       NUL below would write past a zero-length buffer). */
    ret = _my_vnprintf(_string_write_char, (void **)&pnt, size ? size - 1 : 0, fmt, ap);
    if (size) {
        _string_write_char(0, (void **)&pnt);
    }
    va_end(ap);
    return (ret);
}

extern "C" int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
    char *pnt = str;
    /* size==0: write nothing (see snprintf above). */
    int ret = _my_vnprintf(_string_write_char, (void **)&pnt, size ? size - 1 : 0, fmt, ap);
    if (size) {
        _string_write_char(0, (void **)&pnt);
    }
    return (ret);
}

static bool _is_space(char c)
{
    return (c == ' ') || (c == '\n') || (c == '\r') || (c == '\t');
}

// Reads one number in the given radix, and says whether there was one. Leaves pos on
// the first character it did not use, so the caller can match what follows.
static bool _conv(const char *buf, int *pos, int radix, int *result)
{
    int p = *pos;
    int value = 0;
    int digits = 0;
    bool negative = false;

    while (_is_space(buf[p])) {
        p++;
    }
    if ((buf[p] == '-') || (buf[p] == '+')) {
        negative = (buf[p] == '-');
        p++;
    }
    for (;;) {
        char c = buf[p];
        int digit;
        if ((c >= '0') && (c <= '9')) {
            digit = c - '0';
        } else if ((radix > 10) && (c >= 'A') && (c <= 'F')) {
            digit = (c - 'A') + 10;
        } else if ((radix > 10) && (c >= 'a') && (c <= 'f')) {
            digit = (c - 'a') + 10;
        } else {
            break;
        }
        value = (value * radix) + digit;
        digits++;
        p++;
    }
    if (!digits) {
        return false;
    }
    *result = negative ? -value : value;
    *pos = p;
    return true;
}

// A small scanf. It follows the C rules that its callers are written against: a
// conversion that does not happen is not counted and does not store anything, a
// literal in the format has to match the input, and whitespace in the format matches
// any run of whitespace including none. Scanning stops at the first mismatch.
//
// One deliberate simplification: an input that ends before the first conversion
// returns 0, where C returns EOF. No caller distinguishes the two.
extern "C" int _vscanf(const char *buf, const char *fmt, va_list ap)
{
	int pos = 0;
	int count = 0;

	for (int i = 0; fmt[i]; i++) {
		if (_is_space(fmt[i])) {
			while (_is_space(buf[pos])) {
				pos++;
			}
			continue;
		}
		if (fmt[i] != '%') {
			if (buf[pos] != fmt[i]) {
				return count;
			}
			pos++;
			continue;
		}

		i++;
		if (!fmt[i]) {
			return count;
		}
		void *pntr = va_arg(ap, void *);
		int result = 0;
		switch (fmt[i]) {
		case 'd':
			if (!_conv(buf, &pos, 10, &result)) {
				return count;
			}
			*((int *)pntr) = result;
			break;
		case 'x':
			if (!_conv(buf, &pos, 16, &result)) {
				return count;
			}
			*((int *)pntr) = result;
			break;
		case 'c':
			if (!buf[pos]) {
				return count;
			}
			*((char *)pntr) = buf[pos++];
			break;
		case 's': {
			// No field width is supported, so the destination has to be big enough
			// for the whole word. No caller in this firmware uses %s.
			char *dest = (char *)pntr;
			while (_is_space(buf[pos])) {
				pos++;
			}
			if (!buf[pos]) {
				return count;
			}
			while (buf[pos] && !_is_space(buf[pos])) {
				*(dest++) = buf[pos++];
			}
			*dest = 0;
			break;
		}
		default:
			return count;
		}
		count++;
	}
	return count;
}

extern "C" int sscanf(const char *buf, const char *fmt, ...)
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
