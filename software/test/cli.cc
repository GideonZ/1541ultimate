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

#define P_END ARRAY({ NULL, 0})


CLI_COMMAND(help, "This function is supposed to help you.", ARRAY( {{ "command", P_ALLOW_UNNAMED }, P_END} ), "yuk")
{
    mprintf("My description is: %s\n", cli_help.descr);
    args.dump_args();
    args.Validate(cli_help);
    printf("After validate:\n");
    args.dump_args();

    int known_params = NR_OF_EL(c_paramshelp);
    mprintf("Number of defined params: %d\n", known_params);
    for(int i=0; i<known_params; i++) {
        mprintf("Param %d: %s\n", i, cli_help.params[i].long_param);
    }
    mprintf("I got %d args.\n", args.get_elements());
    mprintf("Subcommand arg = '%s'\n", args["command"]);
    return 200;
}

CLI_COMMAND(copy, "This function will copy a file.", ARRAY( {{ "from", P_ALLOW_UNNAMED | P_REQUIRED }, { "to", P_ALLOW_UNNAMED | P_REQUIRED }, P_END} ), "")
{
    mprintf("My description is: %s\n", cli_copy.descr);
    args.Validate(cli_copy);
    printf("After validate:\n");
    args.dump_args();
    return 0;
}

int main(int argc, char** argv)
{
    Stream_StdOut out;
    Args args("hello -a --\"command=koeiereet\"=mest garnaal \" iets met spaties \" --sushi --rusland= --\"ultimate\"=a -yup=3 --\"e nd\"=\"/Usb0/bl ah.d64\" -k --kak=");
    args.dump_args();

    IndexedList<const CliCommand_t *> *list = getCliCommandList();
    printf("The command list contains: %d items.\n", list->get_elements());
    const CliCommand_t *t = (*list)[1];
    if (t) {
        printf("Command name: %s\n", t->cmd);
        args.Parse("--from=from_file --to=to_file");
        t->proc(out, args);
        args.Parse("--to=to_file --from=\"from_file\"");
        t->proc(out, args);
        args.Parse("--to=to_file from_file");
        t->proc(out, args);
        args.Parse("from to");
        t->proc(out, args);
        args.Parse("from");
        t->proc(out, args);
        args.Parse("--to=to_file");
        t->proc(out, args);
        args.Parse("");
        t->proc(out, args);
        args.Parse("--reset");
        t->proc(out, args);
    }
    return 0;
}
