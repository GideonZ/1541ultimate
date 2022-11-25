#ifndef CLI_H
#define CLI_H

#include <stdio.h>
#include "indexed_list.h"
#include "stream.h"
#include "dict.h"
#include "mystring.h"

/* ************************************************************************* */
/* Defines.                                                                  */
/* ************************************************************************* */

#define mprintf(...) outf.format(__VA_ARGS__)

#define P_REQUIRED      0x01
#define P_ALLOW_UNNAMED 0x02
#define P_SWITCH        0x04

typedef struct {
    const char *long_param;
    int  flags;
} Param_t;

class Args;

typedef struct {
    const char *cmd;                           // Command string 
    int (*proc)(Stream& outf, Args& args);     // Procedure handling the command . Return value is HTTP error code
    const char *descr;                         // Description of the command 
    const Param_t *params;                     // Supported Parameters
    const char *switches;                      // Known / supported switches
} CliCommand_t;

class Args : public Dict<const char *, const char *>
{
    IndexedList<const char *>unnamed;
    char *cmd;
    mstring switches;

    char *Trim(char *word)
    {
        if (!word) {
            return NULL;
        }
        while (*word == ' ')
            word++;
        int n=strlen(word)-1;
        while(n >= 0 && (word[n] == ' ')) {
            word[n--] = 0;
        }
        if (*word == '\"' && word[n] == '\"') {
            word[n--] = 0;
            word++;
        }
        return word;
    }

    void long_arg(char *str)
    {
        int len = strlen(str);
        char *val = NULL;
        bool quoted = false;
        for (int i=0;i<len;i++) {
            if((str[i] == '=') && !quoted) {
                str[i] = 0;
                val = &str[i+1];
                break;
            } else if(str[i] == '\"') {
                quoted = !quoted;
            }
        }
        set(Trim(str), Trim(val));
    }

    void short_arg(char *str)
    {
        int len = strlen(str);
        for (int i=0;i<len;i++) {
            char *val = NULL;
            if(str[i+1] == '=') {
                str[i+1] = 0;
                set(Trim(&str[i]), Trim(&str[i+2]));
                break;
            } else { // single letter 
                switches += str[i];
            }
        }
    }

public:
    Args() : Dict(8, NULL, NULL), unnamed(4, NULL), switches(8)
    {
    }

    Args(const char *args) : Dict(8, NULL, NULL), unnamed(4, NULL), switches(8)
    {
        Parse(args);
    }

    virtual ~Args()
    {
        delete[] cmd;
    }

    int Parse(const char *args)
    {
        clear();
        unnamed.clear_list();
        switches = "";

        // This function takes an argument string and unravels it into a dictionary.
        // The string can contain switches, named arguments, or unnamed arguments:
        // -a : Switch a set to true.
        // -ab : Switch a set to true, and switch b set to true 
        // --longname=string: Long named argument with 'string' as value
        // blah: unnamed argument with 'blah' as value. The unnamed arguments are stored in a separate list and
        // matched later to the function specification 
        
        // Let's first split the string in pieces by replacing the spaces with nulls. Our own cmd is the owner
        // so we can have null-terminated string slices as an array of const char *.
        bool quoted = false;

        IndexedList<char *> slices(8, NULL);
        int len = strlen(args);
        cmd = new char[len+1];
        strcpy(cmd, args);
        int last = 0;
        for (int i=0;i<=len;i++) {
            if (!cmd[i] || (cmd[i] == ' ' && !quoted)) {
                cmd[i] = 0;
                if (i - last >= 1) {
                    slices.append(cmd + last);
                }
                last = i+1;
            } else if(cmd[i] == '\"') {
                quoted = !quoted;
            }
        }

        // Now lets go through all the slices and pick those that start with -- and -.
        // All others will be added to unnamed.
        int words = slices.get_elements();
        for(int i=0;i<words;i++) {
            if (slices[i][0] == '-') {
                if (slices[i][1] == '-') {
                    long_arg(slices[i]+2);
                } else {
                    short_arg(slices[i]+1);
                }
            } else {
                if (strlen(slices[i]) > 0) {
                    unnamed.append(Trim(slices[i]));
                }
            }
        }
        return 1;
    }

    void Validate(const CliCommand_t& def)
    {
        // checks if all elements from the dictionary are
        // actually valid commands
        uint32_t matched = 0;
        for (int i=0; i < get_elements(); i++) {
            const char *k = get_key(i);
            const Param_t *p = def.params;
            bool found = false;
            for(int j=0; p[j].long_param; j++) {
                if (strcmp(k, p[j].long_param) == 0) {
                    matched |= (1 << j);
                    found = true;
                    break;
                }
            }
            if(!found) {
                printf("--> Function %s does not have parameter %s\n", def.cmd, k);
            }
        }
        // now see if there are any unmatched parameters
        // that could be attached to the unnamed args
        const Param_t *p = def.params;
        for(int i=0; p[i].long_param; i++) {
            if (!(matched & (1 << i))) {
                if(p[i].flags & P_ALLOW_UNNAMED) {
                    const char *arg = unnamed[0];
                    if (arg) {
                        unnamed.remove_idx(0);
                        set(p[i].long_param, arg);
                        matched |= (1 << i);
                    }
                }
            }
        }

        // Now check if all required arguments are actually given
        p = def.params;
        for(int i=0; p[i].long_param; i++) {
            if (!(matched & (1 << i))) {
                if(p[i].flags & P_REQUIRED) {
                    printf("--> Function %s requires parameter %s\n", def.cmd, p[i].long_param);
                }
            }
        }
    }

    void dump_args(void)
    {
        printf("Named Arguments:\n");
        dump();
        printf("Unnamed Arguments:\n");
        for(int i=0;i<unnamed.get_elements();i++) {
            printf(" '%s'\n", unnamed[i]);
        }
        printf("Switches: %s\n", switches.c_str());
    }

};

#define CLI_COMMAND(NAME, DESCR, PARAMS, SWITCHES)            \
    static int Do ## NAME(Stream& outf, Args& args);          \
    const Param_t c_params ## NAME[] =  PARAMS;               \
    const CliCommand_t cli_ ## NAME =                         \
    {   ((const char*)                   #NAME     ),         \
        ((int(*)(Stream&, Args&))        Do ## NAME),         \
        ((const char*)                   DESCR     ),         \
        ((const Param_t *)               c_params ## NAME ),  \
        ((const char*)                   SWITCHES  ),         \
    };                                                        \
    CliCommandRegistrar RegisterCli_ ## NAME(& cli_ ## NAME); \
    static int Do ## NAME(Stream& outf, Args& args)


IndexedList<const CliCommand_t *> *getCliCommandList(void);

class CliCommandRegistrar
{
public:
    CliCommandRegistrar(const CliCommand_t *func) {
        getCliCommandList()->append(func);
    }
};


#endif /* CLI_H */
