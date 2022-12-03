#ifndef ROUTES_H
#define ROUTES_H

#include "cli.h"
#include "url.h"

class ArgsURI;

typedef struct {
    const char *route;                         // String pointer to route
    const char *cmd;                           // Command string 
    int (*proc)(Stream& outf, ArgsURI& args);  // Procedure handling the command . Return value is HTTP error code
    const char *descr;                         // Description of the command 
    const Param_t *params;                     // Supported Parameters
} ApiCall_t;

// Map of routes to lists of commands
Dict<const char *, IndexedList<const ApiCall_t *>*> *getRoutesList(void);

class ArgsURI : public Args
{
    UrlComponents comps;
public:
    ArgsURI() : Args()
    {
        bzero(&comps, sizeof(comps));
    }

    virtual ~ArgsURI()
    {
        ClearAll();
    }

    void ClearAll()
    {
        // clear comps
        if(comps.url_copy) {
            free(comps.url_copy);
        }
        if(comps.querystring_copy) {
            free(comps.querystring_copy);
        }
        if (comps.parameters) {
            free(comps.parameters);
        }

        // clear args
        ClearArgs();
    }

    static const ApiCall_t *find_api_call(const char *route, const char *command)
    {
        IndexedList<const ApiCall_t *> *commandTable = (*getRoutesList())[route];
        if (!commandTable) {
            printf("Unknown route %s\n", route);
            return NULL;
        }
        for (int i = 0; i < commandTable->get_elements(); i++) {
            const ApiCall_t *t = (*commandTable)[i];
            if (strcmp(command, t->cmd)==0) {
                return t;
            }
        }
        return NULL;
    }

    const ApiCall_t * ParseReqHeader(HTTPReqHeader *hdr)
    {
        // Clean up comps
        ClearAll();

        int ret = parse_header_to_url_components(hdr, &comps);

        if (ret != 0) {
            return NULL;
        }

        // success!
        // Now parse the query string into a dict
        comps.querystring_copy = strdup(comps.querystring);
        char *value, *token, *name;
        char *copy = comps.querystring_copy;

        while ((value = token = strsep(&copy, "&")) != NULL) {
            name = strsep(&value, "=");
            if (value == NULL) {
                set(name, "");
            } else {
                set(name, value);
            }
        }
        add_unnamed(comps.path);

        return find_api_call(comps.route, comps.command);
    }

    const char *get_path(void)
    {
        return get_unnamed_arg(0);
    }

    int Validate(const ApiCall_t& def)
    {
        int errors = 0;
        // checks if all elements from the dictionary are
        // actually valid parameters
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
                errors ++;
            }
        }

        // Now check if all required arguments are actually given
        const Param_t *p = def.params;
        for(int i=0; p[i].long_param; i++) {
            if (!(matched & (1 << i))) {
                if(p[i].flags & P_REQUIRED) {
                    printf("--> Function %s requires parameter %s\n", def.cmd, p[i].long_param);
                    errors ++;
                }
            }
        }

        return errors;
    }    
};

#define MCONC(A,B) A##B

#define API_CALL(ROUTE, COMMAND, DESCR, PARAMS)                                                                        \
    static int Do##ROUTE##_##COMMAND(Stream &outf, ArgsURI &args);                                                        \
    const Param_t c_params_##ROUTE##_##COMMAND[] = PARAMS;                                                             \
    const ApiCall_t http_##ROUTE##_##COMMAND = {                                                                       \
        ((const char *)#ROUTE),                                                                                        \
        ((const char *)#COMMAND),                                                                                      \
        ((int (*)(Stream &, ArgsURI &))Do##ROUTE##_##COMMAND),                                                            \
        ((const char *)DESCR),                                                                                         \
        ((const Param_t *)c_params_##ROUTE##_##COMMAND),                                                               \
    };                                                                                                                 \
    ApiCallRegistrar RegisterApiCall_##ROUTE##_##COMMAND(&http_##ROUTE##_##COMMAND);                                   \
    static int Do##ROUTE##_##COMMAND(Stream &outf, ArgsURI &args)

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
