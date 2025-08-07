#include "pattern.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <cctype>
/*
-------------------------------------------------------------------------------
							pattern_match
							=============
  Abstract:

	Sees if the find string can be matched to a fixed string. Supports
	wildcards * and ?.

-------------------------------------------------------------------------------
*/

static bool hex2bin(char c, uint8_t& out)
{
    if ((c >= '0') && (c <= '9')) {
        out = (uint8_t)(c - '0');
        return true;
    }
    if ((c >= 'A') && (c <= 'F')) {
        out = (uint8_t)(c - 'A') + 10;
        return true;
    }
    if ((c >= 'a') && (c <= 'f')) {
        out = (uint8_t)(c - 'a') + 10;
        return true;
    }
    return false;
}

static char get_escaped_char(const char *&p, bool &escape)
{
    char ret;
    while(true) {
        if (!(*p))
            return 0;

        if (escape) {
            if (*p == '}') {
                escape = false;
                p++;
                continue;
            }
            char h = p[0];
            char l = p[1];
            if (!h || !l) {
                ret = 0;
                break;
            }
            uint8_t hb, lb;
            if (!hex2bin(h, hb) || !hex2bin(l, lb)) {
                escape = false;
                ret = '{';
                break;
            } else {
                ret = (hb << 4) | lb;
                p += 2;
                break;
            }
        } else {
            if (*p == '{') {
                escape = true;
                p++;
            } else {
                ret = *p;
                p++;
                break;
            }
        }
    }
    return ret;
}

bool pattern_match(const char *pattern, const char *fixed, bool case_sensitive)
{
    const char *p;
    const char *f;
    char cp, cf;

    p = pattern;
    f = fixed;
    do {
        if(!(*p)) { // end of pattern
            return (*f == '\0'); // if the fixed is also at the end, we have a match,
                                 // if not, then our pattern is too short and we don't match
        }
        if(!(*f)) { // end of fixed string
            if(*p == '*') // check special case foo* == foo.
                return true;
            else
                return false; // our pattern string was longer
        }
        if(*p == '*') {
            p++; // skip the * and set pointer to next character
            if(!(*p)) // end of our pattern, so we have a match
                return true;

            // now, recurse and try to find the pattern AFTER the * in the fixed string
            while(*f) {
                bool match = pattern_match(p, f, case_sensitive);
                if(match)
                    return true;
                f++;
            }
            return false;
        } else if(*p == '?') {
            // assume equality; continue
        } else {
    		if(!case_sensitive) {
    			cp = toupper(*p);
    			cf = toupper(*f);
            } else {
                cp = *p;
                cf = *f;
            }
            if(cf != cp)
                return false; // two character inequal
        } 
        f++;
        p++;
    } while(true);
        
	return false; // never gets here.
}

bool pattern_match_escaped(const char *pattern, const char *fixed, bool case_sensitive, bool p_esc, bool f_esc)
{
    const char *p;
    const char *f;
    const char *prev_f;
    char cp, cf, rp;

    p = pattern;
    f = fixed;
    do {
        rp = *p;
        prev_f = f;
        cf = get_escaped_char(f, f_esc);
        cp = get_escaped_char(p, p_esc);

        if(!cp) { // end of pattern
            return (cf == '\0'); // if the fixed is also at the end, we have a match,
                                 // if not, then our pattern is too short and we don't match
        }
        if(!cf) { // end of fixed string
            if(rp == '*') // check special case foo* == foo.
                return true;
            else
                return false; // our pattern string was longer
        }
        if(rp == '*') {
            if(!(*p)) // end of our pattern, so we have a match
                return true;

            // now, recurse and try to find the pattern AFTER the * in the fixed string
            // first reset F, as we should not consume the char under the *
            f = prev_f;
            while(cf) {
                printf("Recursive match: %s %s\n", p, f);
                bool match = pattern_match_escaped(p, f, case_sensitive, p_esc, f_esc);
                if(match)
                    return true;
                cf = get_escaped_char(f, f_esc);
            }
            return false;
        } else if(rp == '?') {
            // assume equality; continue
        } else {
            if(!case_sensitive) {
                cp = toupper(cp);
                cf = toupper(cf);
            }
            if(cf != cp)
                return false; // two character inequal
        }
    } while(true);

    return false; // never gets here.
}

int split_string(char sep, char *s, char **parts, int maxParts)
{
    int len = strlen(s);
    int idx = 0;
    parts[idx++] = s;
    for(int i=0;(i<len) && (idx < maxParts);i++) {
        if (s[i] == sep) {
            s[i] = 0;
            parts[idx++] = s + i + 1;
        }
    }
    return idx;
}

bool isEmptyString(const char *c)
{
    if (!c)
        return true;
    if (strlen(c) == 0)
        return true;
    return false;
}

#ifdef TESTPTRN
#include <stdio.h>

int main()
{
    struct test_vector
    {
        bool case_sens;
        const char *pattern;
        const char *fixed;
        bool expected;
    };
    
    struct test_vector vectors[27] = {
        { true, "", "", true },
        { true, "*", "", true },
        { true, "?", "", false },
        { true, "?", "{88}", true },
        { true, "g*n", "{676964656f6e}", true },
        { true, "g*n", "{676964656f6e}", true },
        { true, "g*n{7a}", "{676964656f6e7a}", true },
        { true, "g{2a}n{7a}", "{676964656f6e7a}", false },
        { true, "gideon", "gideon", true },
        { true, "lili*", "liliana", true },
        { true, "lili*", "lili", true },
        { true, "lili*a", "liliena", true },
        { true, "lili*a", "l{69}liana", true },
        { true, "lili*a", "liliane", false },
        { true, "lili*a", "liliana martinez", false },
        { true, "lili*mar*", "liliana martinez", true },
        { true, "lili*mar*ez", "liliana martinez", true },
        { true, "lili*m?r*ez", "liliana mertinez", true },
        { true, "lil?", "lili", true },
        { true, "lil?", "lil", false },
        { true, "lili", "gideon", false },
        { true, "gideon", "lili", false },
        { true, "lili", "lilian", false },
        { false, "lil?", "LILI", true },
        { false, "L{69}L?", "lili", true },
        { true,  "LIL?", "lili", false },
        { true,  "lilian", "lili", false } };

        
    int errors = 0;
    for(int i=0;i<27;i++) {
        bool test = pattern_match_escaped(vectors[i].pattern, vectors[i].fixed, vectors[i].case_sens);
        if(test != vectors[i].expected) {
            errors ++;
            printf("Error! wrong result for: %s =? %s. Case = %d\n",
                vectors[i].pattern, vectors[i].fixed, vectors[i].case_sens);
        }
    }
    printf("Total number of errors: %d\n", errors);

    const char *escaped = "Dit {69}s {}een t{657374}je. {yy}{123456abcdefghij}{";
    char n;
    const char *p = escaped;
    bool esc = false;
    while(n = get_escaped_char(p, esc)) {
        printf("%02x: %c\n", n, n);
    }

    return errors;
}

#endif

/* some handy functions */
void set_extension(char *buffer, const char *ext, int buf_size)
{
    // try to remove the existing extension
    int name_len = strlen(buffer);
    for(int i=name_len-1;i>=0;i--) {
        if(buffer[i] == '.') {
            buffer[i] = 0;
            break;
        }
    }

    add_extension(buffer, ext, buf_size);
}

void add_extension(char *buffer, const char *ext, int buf_size)
{
    // skip leading dots of extension to set
    while(*ext == '.') {
        ext++;
    }
    int ext_len = strlen(ext) + 1; // +1 because of dot
    if(buf_size < 1+ext_len)
        return; // cant append, even to an empty base

    if (!(*ext)) { // there is nothing to append
        return;
    }

    int name_len = strlen(buffer);
    if(name_len + ext_len + 1 > buf_size) {
        buffer[buf_size-ext_len-1] = 0; // truncate to make space for extension!
        name_len = buf_size-ext_len-1;
    }
    buffer[name_len++] = '.';
    // strcat + tolower
    while(*ext) {
        buffer[name_len++] = tolower(*(ext++));
    } buffer[name_len] = 0;
}

int fix_filename(char *buffer)
{
    const char illegal[] = "\"*:/<>\\?|,\x7F";
    int illegal_count = strlen(illegal);
    int len = strlen(buffer);

    int replacements = 0;
    for(int i=0;i<len;i++) {
        for(int j=0;j<illegal_count;j++) {
            if(buffer[i] == illegal[j]) {
                buffer[i] = '_';
                replacements ++;
            }
        }
    }
    return replacements;
}

int get_extension(const char *name, char *ext, bool caps)
{
    int len = strlen(name);
    ext[0] = 0;

    for (int i=len-1;i>=0;i--) {
        if (name[i] == '.') { // last . found
            len = i; // to return length of base name
            for(int j=0;j<3;j++) { // copy max 3 chars
                ext[j+1] = 0; // the char after the current is always end
                if (!name[i+1+j])
                    break;
                if(caps) {
                    ext[j] = toupper(name[i+1+j]);
                } else {
                    ext[j] = name[i+1+j];
                }
            }
            break;
        }
    }
    return len;
}

// Truncates the filename such that the resulting string will always fit in the
// buffer, and the extension is copied, if at all possible.
// Example, if bufsize is 32, then the maximum length of the string will be 31.
// This means that if the length of the extension is 3, the base string will
// be at most 27 chars.
void truncate_filename(const char *orig, char *buf, int bufsize)
{
    buf[0] = 0;
    if (bufsize < 5) {
        return;
    }
    char ext[4];
    get_extension(orig, ext);

    strncpy(buf, orig, bufsize);
    buf[bufsize-5] = 0;

    set_extension(buf, ext, bufsize);
}

const char *get_filename(const char *path)
{
    int n = strlen(path);
    while(n) {
        if (path[n] == '/') {
            return path + n + 1;
        }
        n--;
    }
    return path;
}

void petscii_to_fat(const char *pet, char *fat, int maxlen)
{
    // clear output string
    const char *hex = "0123456789ABCDEF";
    bool escape = false;
    int i = 0;
    while(*pet) {
        char p = *(pet++);
        if ((p < 32) || (p >= 96) || (p == ':') || (p == '/') ||
                (p == '\\') || (p == '\x22') || (p == ',') ||
                (p == '<') || (p == '>') ) {  // '|' > 96 ;)

            if ((i + 4) >= maxlen) {
                break;
            }
            if (!escape) {
                fat[i++] = '{';
                escape = true;
            }
            fat[i++] = hex[((uint8_t)p) >> 4];
            fat[i++] = hex[p & 15];
        } else {
            if ((i + 2) >= maxlen) {
                break;
            }
            if (escape) {
                fat[i++] = '}';
                escape = false;
            }
            fat[i++] = tolower(p);
        }
    }
    if (escape) {
        fat[i++] = '}';
        escape = false;
    }
    fat[i] = 0;
}

void fat_to_petscii(const char *fat, bool cutExt, char *pet, int len, bool term)
{
    int i = 0, k = 0;
    bool escape = false;
    int max = strlen(fat);

    if (cutExt) {
        for(int j=max-1;j>=max-4;j--) {
            if(fat[j] == '.') {
                max = j;
                break;
            }
        }
    }

    while(fat[k] && (k < max) && (i < len)) {
        char f = fat[k];
        if (f == '{') {
            escape = true;
            k++;
        }
        if (escape) {
            char h = fat[k+0];
            char l = fat[k+1];
            if (!h || !l) {
                break;
            }
            uint8_t hb, lb;
            if (!hex2bin(h, hb) || !hex2bin(l, lb)) {
                escape = false;
                // do not advance
            } else {
                pet[i++] = (hb << 4) | lb;
                k += 2;
            }
        } else {
            if (f == '}') {
                escape = false;
                k++;
            } else {
                pet[i++] = toupper(f);
                k++;
            }
        }
    }
    if (term) {
        pet[i] = 0;
    }
}

int read_line(const char *buffer, int index, char *out, int outlen)
{
    int i = 0;
    // trim leading spaces and tabs
    while ((buffer[index] == 0x20) || (buffer[index] == 0x09)) {
        index ++;
    }
    while ((buffer[index] != 0x0A) && (buffer[index] != 0x00)) {
        if (buffer[index] != 0x0D) {
            if (i < (outlen-1)) {
                out[i++] = buffer[index];
            }
        }
        index++;
    }
    if ((buffer[index] == 0x0A) || (buffer[index] == 0x00)) {
        index++;
    }
    out[i] = 0;
    return index;
}
