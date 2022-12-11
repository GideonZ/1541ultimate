#ifndef ROUTES_H
#define ROUTES_H

#include "cli.h"
#include "stream_textlog.h"

extern "C" {
    #include "url.h"
}

class ArgsURI;
typedef int (*APIFUNC)(ArgsURI& args, HTTPReqMessage *req, HTTPRespMessage *resp, void *body);
typedef void *(*BodyHandlerSetupFunc_t)(HTTPReqMessage *req, HTTPRespMessage *resp, APIFUNC func, ArgsURI *args);

typedef struct {
    const char *route;                         // String pointer to route
    const char *cmd;                           // Command string 
    APIFUNC proc;                              // Procedure handling the command
    BodyHandlerSetupFunc_t body_handler;       // Setup function for streaming body data (NULL for ditch or manual)
    const Param_t *params;                     // Supported Parameters
} ApiCall_t;

// Map of routes to lists of commands
Dict<const char *, IndexedList<const ApiCall_t *>*> *getRoutesList(void);

class ArgsURI : public Args
{
    UrlComponents comps;
    StreamTextLog errortext;
public:
    ArgsURI() : Args(), errortext(1024)
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
                errortext.format("--> Function %s does not have parameter %s<br>\n", def.cmd, k);
                errors ++;
            }
        }

        // Now check if all required arguments are actually given
        const Param_t *p = def.params;
        for(int i=0; p[i].long_param; i++) {
            if (!(matched & (1 << i))) {
                if(p[i].flags & P_REQUIRED) {
                    errortext.format("--> Function %s requires parameter %s<br>\n", def.cmd, p[i].long_param);
                    errors ++;
                }
            }
        }

        return errors;
    }

    const char *get_errortext()
    {
        return errortext.getText();
    }
};

#define MCONC(A,B) A##B

#define API_CALL(ROUTE, COMMAND, BODYSETUP, PARAMS)                                                                    \
    static void Do##ROUTE##_##COMMAND(ArgsURI &args, HTTPReqMessage *req, HTTPRespMessage *resp, void *body);           \
    const Param_t c_params_##ROUTE##_##COMMAND[] = PARAMS;                                                             \
    const ApiCall_t http_##ROUTE##_##COMMAND = {                                                                       \
        ((const char *)#ROUTE),                                                                                        \
        ((const char *)#COMMAND),                                                                                      \
        ((APIFUNC)Do##ROUTE##_##COMMAND),                                                                              \
        ((BodyHandlerSetupFunc_t)BODYSETUP),                                                                           \
        ((const Param_t *)c_params_##ROUTE##_##COMMAND),                                                               \
    };                                                                                                                 \
    ApiCallRegistrar RegisterApiCall_##ROUTE##_##COMMAND(&http_##ROUTE##_##COMMAND);                                   \
    static void Do##ROUTE##_##COMMAND(ArgsURI &args, HTTPReqMessage *req, HTTPRespMessage *resp, void *body)

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

void build_response(HTTPRespMessage *resp, int code, const char *fmt, ...);

#endif // ROUTES_H
