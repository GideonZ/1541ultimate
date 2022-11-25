/*
 * cli.cc
 *
 *  Created on: May 21, 2021
 *      Author: gideon
 */
#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION

#include "cli.h"
#include "stream_stdout.h"

void outbyte(int byte)
{
    putc(byte, stdout);
}
#define NR_OF_EL(a)		(sizeof(a) / sizeof(a[0]))

IndexedList<const CliCommand_t *> *getCliCommandList(void) {
    static IndexedList<const CliCommand_t *>  cliCommands(24, 0);
    return &cliCommands;
}
#define ARRAY(...) __VA_ARGS__

CLI_COMMAND(help, "This function is supposed to help you.", ARRAY( {{ "command", "c", NULL }} ))
{
    mprintf("My description is: %s\n", cli_help.descr);
    int known_params = NR_OF_EL(c_paramshelp);
    mprintf("Number of defined params: %d\n", known_params);
    for(int i=0; i<known_params; i++) {
        mprintf("Param %d: %s\n", i, cli_help.params[i].long_param);
    }
    mprintf("I got %d args.\n", args.get_elements());
    mprintf("Subcommand arg = '%s'\n", args["command"]);
    return 200;
}

int main(void)
{
    Stream_StdOut out;
    Args args("hello -a --command=koeiereet garnaal --sushi --rusland= -yup=3 --end=/Usb0/blah.d64 -k= --kak=");
    args.dump_args();

/*
    IndexedList<const CliCommand_t *> *list = getCliCommandList();
    printf("The command list contains: %d items.\n", list->get_elements());
    const CliCommand_t *t = (*list)[0];
    if (t) {
        printf("Command name: %s\n", t->cmd);
        printf("%s\n", t->descr);
        args.set("command", "koeiereet");
        t->proc(out, args);
    }
*/
    return 0;
}
