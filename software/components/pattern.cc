#include "pattern.h"
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
bool pattern_match(char *pattern, char *fixed, bool case_sensitive)
{
	bool match = true;
    char *p, *f;
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
