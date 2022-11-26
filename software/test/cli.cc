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

IndexedList<const CliCommand_t *> *getCliCommandList(void) {
    static IndexedList<const CliCommand_t *>  cliCommands(24, 0);
    return &cliCommands;
}

static void display_cmd(Stream& outf, const CliCommand_t *t, bool list_params)
{
    mprintf("%-12s: %s\n", t->cmd, t->descr);
    if (list_params) {
        for (const Param_t *p = t->params; p->long_param; p++) {
            mprintf("  --%s (%s)\n", p->long_param, (p->flags & P_REQUIRED) ? "REQUIRED" : "OPTIONAL");
        }
        mprintf("  Supported switches: -%s\n\n", t->switches);
    }
}

static const CliCommand_t *findCliCommand(mstring& cmd)
{
    IndexedList<const CliCommand_t *> *list = getCliCommandList();
    for (int i=0; i < list->get_elements(); i++) {
        const CliCommand_t *t = (*list)[i];
        if (cmd == t->cmd) {
            return t;
        }
    }
    return NULL;    
}

static const CliCommand_t *findCliCommand(const char *cmd)
{
    mstring c(cmd);
    return findCliCommand(c);
}

int run_cli_cmd(Stream& out, const char *line)
{
    // Search for the first space. Everything before the space is the command; everything after it are the args.
    // We may be tempted to simply parse the whole line as args and take the first unnamed string, but if we'd
    // do this, command lines like '-R copy a b' would also work (having a switch before the command!)
    const char *argstr = NULL;
    int cmdlen = strlen(line);
    for(int i=0;line[i];i++) {
        if (line[i] == ' ') {
            argstr = line + i + 1;
            cmdlen = i;
            break;
        }
    }
    // trim \n if at the end of command
    if (line[cmdlen-1] == '\n')
        cmdlen--;

    // the command is now line[0..cmdlen] (exclusive)
    mstring cmd(line, 0, cmdlen-1);

    // find the command in the available list of commands
    const CliCommand_t *t = findCliCommand(cmd);
    if(t) {
        // Command found! parse args, and call the function
        Args args(argstr);
        return t->proc(out, args);
    }

    // Command not found
    printf("--> Command %s not found!\n", cmd.c_str());
    return -1;
}

CLI_COMMAND(help, "This function is supposed to help you.", ARRAY( {{ "command", P_ALLOW_UNNAMED }, P_END} ), "p", false)
{
    if (args.Validate(cli_help)) {
        printf("Invalid arguments\n");
        return 1;
    }

    bool list_params = args.check_switch('p');
    if (!(args["command"])) {
        // no unnamed arguments given; list all commands
        mprintf("Known commands:\n");
        IndexedList<const CliCommand_t *> *list = getCliCommandList();
        for (int i=0; i < list->get_elements(); i++) {
            const CliCommand_t *t = (*list)[i];
            display_cmd(outf, t, list_params);
        }
    } else {
        const CliCommand_t *t = findCliCommand(args["command"]);
        if (t) {
            display_cmd(outf, t, list_params);
        } else {
            mprintf("'%s' is not a known command!\n", args["command"]);
        }
    }

    return 0;
}

CLI_COMMAND(copy, "This function will copy a file.", ARRAY( { { "to", P_ALLOW_UNNAMED | P_REQUIRED | P_LAST_ARG }, { "from", P_ALLOW_UNNAMED | P_REQUIRED }, P_END} ), "R", true)
{
    if (args.Validate(cli_copy) == 0) {
        args.dump_args();
    } else {
        printf("Invalid arguments\n");
        return 1;
    }
    return 0;
}

CLI_COMMAND(quit, "Exit this demo.", ARRAY( { P_END } ), "", false)
{
    // no need to check the parameters
    exit(0);
    return 0;
}

void test()
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
        args.Parse("-R from");
        t->proc(out, args);
        args.Parse("--to=to_file");
        t->proc(out, args);
        args.Parse("");
        t->proc(out, args);
        args.Parse("--reset");
        t->proc(out, args);
        args.Parse("-S from to");
        t->proc(out, args);
        args.Parse("boom roos vis vuur mus pim --from=kees");
        t->proc(out, args);
    }
}

int main()
{
    Stream_StdOut out;
    char buffer[128];
    char *str;
    do {
        printf("> ");
        str = fgets(buffer, 128, stdin);
        if (str) {
            run_cli_cmd(out, str);
        }
    } while(str);

    return 0;
}

// This is to make smallprintf work on PC.
void outbyte(int byte)
{
    putc(byte, stdout);
}
