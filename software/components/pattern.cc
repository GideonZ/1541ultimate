#include "pattern.h"
#include <string.h>
#include <stdint.h>
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

void split_string(char sep, char *s, char **parts, int maxParts)
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
        char *pattern;
        char *fixed;
        bool expected;
    };
    
    struct test_vector vectors[16] = {
        { true, "", "", true },
        { true, "gideon", "gideon", true },
        { true, "lili*", "liliana", true },
        { true, "lili*", "lili", true },
        { true, "lili*a", "liliena", true },
        { true, "lili*a", "liliana", true },
        { true, "lili*a", "liliane", false },
        { true, "lili*a", "liliana martinez", false },
        { true, "lili*mar*", "liliana martinez", true },
        { true, "lili*mar*ez", "liliana martinez", true },
        { true, "lil?", "lili", true },
        { true, "lil?", "lil", false },
        { true, "lili", "gideon", false },
        { true, "gideon", "lili", false },
        { true, "lili", "lilian", false },
        { true, "lilian", "lili", false } };

        
    int errors = 0;
    for(int i=0;i<16;i++) {
        bool test = pattern_match(vectors[i].pattern, vectors[i].fixed, vectors[i].case_sens);
        if(test != vectors[i].expected) {
            errors ++;
            printf("Error! wrong result for: %s =? %s. Case = %d\n",
                vectors[i].pattern, vectors[i].fixed, vectors[i].case_sens);
        }
    }
    printf("Total number of errors: %d\n", errors);
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

void fix_filename(char *buffer)
{
    const char illegal[] = "\"*:/<>\\?|,\x7F";
    int illegal_count = strlen(illegal);
    int len = strlen(buffer);

    for(int i=0;i<len;i++)
        for(int j=0;j<illegal_count;j++)
            if(buffer[i] == illegal[j])
                buffer[i] = '_';

}

void get_extension(const char *name, char *ext)
{
    int len = strlen(name);
    ext[0] = 0;

    for (int i=len-1;i>=0;i--) {
        if (name[i] == '.') { // last . found
            for(int j=0;j<3;j++) { // copy max 3 chars
                ext[j+1] = 0; // the char after the current is always end
                if (!name[i+1+j])
                    break;
                ext[j] = toupper(name[i+1+j]);
            }
            break;
        }
    }
}

void petscii_to_fat(const char *pet, char *fat)
{
    // clear output string
    const char *hex = "0123456789ABCDEF";
    bool escape = false;
    int i = 0;
    while(*pet) {
        char p = *(pet++);
        if ((p < 32) || (p >= 96) || (p == ':') || (p == '/') ||
                (p == '\\') || (p == '*') || (p == '\x22') ||
                (p == '<') || (p == '>') || (p == '?')) {  // '|' > 96 ;)
            if (!escape) {
                fat[i++] = '{';
                escape = true;
            }
            fat[i++] = hex[((uint8_t)p) >> 4];
            fat[i++] = hex[p & 15];
        } else {
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

void fat_to_petscii(const char *fat, bool cutExt, char *pet, int len, bool term)
{
    int i = 0, k = 0;
    bool escape = false;
    int max = strlen(fat);

    if (cutExt) {
        for(int j=max-1;j>=0;j--) {
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
