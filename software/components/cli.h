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

typedef struct {
    const char *long_param;
    const char *short_param; // pointer because a single char gives alignment issues
    void *default_value; // void *, so it could be anything
} Param_t;

class Args : public Dict<const char *, const char *>
{
    IndexedList<const char *>unnamed;
    char *cmd;
    mstring switches;
public:
    Args(const char *args) : Dict(8, NULL, NULL), unnamed(4, NULL), switches(8)
    {
        // This constructor takes an argument string and unravels it into a dictionary.
        // The string can contain switches, named arguments, or unnamed arguments:
        // -a : Switch a set to true.
        // -ab : Switch a set to true, and switch b set to true 
        // --longname=string: Long named argument with 'string' as value
        // blah: unnamed argument with 'blah' as value. The unnamed arguments are stored in a separate list and
        // matched later to the function specification 
        
        // Let's first split the string in pieces by replacing the spaces with nulls. Our own cmd is the owner
        // so we can have null-terminated string slices as an array of const char *.
        IndexedList<char *> slices(8, NULL);
        int len = strlen(args);
        cmd = new char[len+1];
        strcpy(cmd, args);
        int last = 0;
        for (int i=0;i<=len;i++) {
            if (!cmd[i] || cmd[i] == ' ') {
                cmd[i] = 0;
                if (i - last >= 1) {
                    slices.append(cmd + last);
                }
                last = i+1;
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
                    unnamed.append(slices[i]);
                }
            }
        }
    }

    void long_arg(char *str)
    {
        int len = strlen(str);
        char *val = NULL;
        for (int i=0;i<len;i++) {
            if(str[i] == '=') {
                str[i] = 0;
                val = &str[i+1];
                break;
            }
        }
        set(str, val);
    }

    void short_arg(char *str)
    {
        int len = strlen(str);
        for (int i=0;i<len;i++) {
            char *val = NULL;
            if(str[i+1] == '=') {
                str[i+1] = 0;
                set(&str[i], &str[i+2]);
                break;
            } else { // single letter 
                switches += str[i];
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

    virtual ~Args()
    {
        delete[] cmd;
    }
};

typedef struct {
    const char *cmd;                           // Command string 
    int (*proc)(Stream& outf, Args& args);     // Procedure handling the command . Return value is HTTP error code
    const char *descr;                         // Description of the command 
    // const char* syntax;                     // Syntax of the command; parsed at boot time into dict
    // Dictionary *
    const Param_t *params;
} CliCommand_t;

/*
const Param_t params[] = { {"command", "c", NULL}, };

static int Dohelp(Stream& outf, Args& args);
const CliCommand_t cli_help = {
    "help",
    Dohelp,
    "This function is supposed to help you.",
    {{"command", "c", NULL},},
};
*/

#define CLI_COMMAND(NAME, DESCR, SYNTAX)        \
    static int Do ## NAME(Stream& outf, Args& args);          \
    const Param_t c_params ## NAME[] =  SYNTAX;         \
    const CliCommand_t cli_ ## NAME =                               \
    {   ((const char*)                   #NAME     ),               \
        ((int(*)(Stream&, Args&))        Do ## NAME),               \
        ((const char*)                   DESCR     ),               \
        ((const Param_t *)               c_params ## NAME ),      \
    };                                                        \
    CliCommandRegistrar RegisterCli_ ## NAME(& cli_ ## NAME); \
    static int Do ## NAME(Stream& outf, Args& args)


//        ((Dict *)                        NULL      ),               \
        ((const char*)                   SYNTAX    ),               \


IndexedList<const CliCommand_t *> *getCliCommandList(void);

class CliCommandRegistrar
{
public:
    CliCommandRegistrar(const CliCommand_t *func) {
        getCliCommandList()->append(func);
    }
};


#endif /* CLI_H */
