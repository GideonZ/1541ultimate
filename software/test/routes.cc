#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION
#include "routes.h"
#include "stream_stdout.h"

Dict<const char *, IndexedList<const ApiCall_t *>*> *getRoutesList(void)
{
    static Dict<const char *, IndexedList<const ApiCall_t *>*> HttpRoutes(10, 0, 0);
    return &HttpRoutes;
}

API_CALL(help, empty, "This function is supposed to help you.", ARRAY({{"command", P_ALLOW_UNNAMED}, P_END}))
{
    mprintf("Hah!\n");
    return 0;
}

const ApiCall_t *find_api_call(const char *route, const char *command)
{
    IndexedList<const ApiCall_t *> *commandTable = (*getRoutesList())[route];
    if (!commandTable) {
        printf("Unknown route %s\n", route);
        return NULL;
    }
    for (int i = 0; i < commandTable->get_elements(); i++) {
        const ApiCall_t *t = (*commandTable)[i];
        if (command == t->cmd) {
            return t;
        }
    }
    return NULL;
}

int main()
{
    Stream_StdOut out;
    Args args;

    const ApiCall_t *call = find_api_call("help", "empty");
    if (call) {
        call->proc(out, args);
    } else {
        printf("Unknown!\n");
    }

    return 0;
}

// This is to make smallprintf work on PC.
void outbyte(int byte) { putc(byte, stdout); }
