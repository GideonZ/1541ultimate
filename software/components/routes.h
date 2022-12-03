#ifndef ROUTES_H
#define ROUTES_H

#include "cli.h"
#define MCONC(A,B) A##B

typedef struct {
    const char *route;                         // String pointer to route
    const char *cmd;                           // Command string 
    int (*proc)(Stream& outf, Args& args);     // Procedure handling the command . Return value is HTTP error code
    const char *descr;                         // Description of the command 
    const Param_t *params;                     // Supported Parameters
} ApiCall_t;

#define API_CALL(ROUTE, COMMAND, DESCR, PARAMS)                                                                        \
    static int Do##ROUTE##_##COMMAND(Stream &outf, Args &args);                                                        \
    const Param_t c_params_##ROUTE##_##COMMAND[] = PARAMS;                                                             \
    const ApiCall_t http_##ROUTE##_##COMMAND = {                                                                       \
        ((const char *)#ROUTE),                                                                                        \
        ((const char *)#COMMAND),                                                                                      \
        ((int (*)(Stream &, Args &))Do##ROUTE##_##COMMAND),                                                            \
        ((const char *)DESCR),                                                                                         \
        ((const Param_t *)c_params_##ROUTE##_##COMMAND),                                                               \
    };                                                                                                                 \
    ApiCallRegistrar RegisterApiCall_##ROUTE##_##COMMAND(&http_##ROUTE##_##COMMAND);                                   \
    static int Do##ROUTE##_##COMMAND(Stream &outf, Args &args)

// Map of routes to lists of commands
Dict<const char *, IndexedList<const ApiCall_t *>*> *getRoutesList(void);

class ApiCallRegistrar
{
public:
    ApiCallRegistrar(const ApiCall_t *func) {
        IndexedList<const ApiCall_t *> *commandList = (*getRoutesList())[func->route];
        if (!commandList) {
            commandList = new IndexedList<const ApiCall_t *>(10, 0);
            getRoutesList()->set(func->route, commandList);
        }
        commandList->append(func);
    }
};
#endif // ROUTES_H
