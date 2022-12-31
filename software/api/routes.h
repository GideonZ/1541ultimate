#ifndef ROUTES_H
#define ROUTES_H

#include "cli.h"
#include "stream_ramfile.h"
#include "json.h"
#include "http_codes.h"

extern "C" {
    #include "url.h"
}

class ArgsURI;
class ResponseWrapper;

typedef int (*APIFUNC)(ArgsURI& args, HTTPReqMessage *req, ResponseWrapper *resp, void *body);
typedef void *(*BodyHandlerSetupFunc_t)(HTTPReqMessage *req, HTTPRespMessage *resp, const void *func, ArgsURI *args);

typedef struct {
    HTTPMethod method;                         // Method / Verb
    const char *route;                         // String pointer to route
    const char *cmd;                           // Command string 
    APIFUNC proc;                              // Procedure handling the command
    BodyHandlerSetupFunc_t body_handler;       // Setup function for streaming body data (NULL for ditch or manual)
    int param_count;                           // automatically filled field with number of specified parameters
    const Param_t *params;                     // Supported Parameters
} ApiCall_t;

// Map of routes to lists of commands
Dict<const char *, IndexedList<const ApiCall_t *>*> *getRoutesList(void);

class ResponseWrapper
{
public:
    JSON_Object *json;
    JSON_List *errors;
    HTTPRespMessage *resp;

    ResponseWrapper(HTTPRespMessage *resp) : resp(resp) {
        json = JSON::Obj();
        errors = JSON::List();
    }

    ~ResponseWrapper() {
        delete json;
        if (errors) {
            delete errors;
        }
    }

    const char *return_codestr(int code) {
        const char *reply = (code == 200)   ? "OK"
                            : (code == 400) ? "Bad Request"
                            : (code == 403) ? "Forbidden"
                            : (code == 404) ? "Not Found"
                                            : "Unknown";
        return reply;
    }

    static int stream_body(void *context, uint8_t *buf, int len)
    {
        StreamRamFile *log = (StreamRamFile *)context;
        int result = log->read((char *)(void *)buf, len);
        if (!result) {
            delete log;
        }
        return result;
    }

    void json_response(int code)
    {
        json->add("errors", errors);
        errors = NULL;

        const char *return_str = return_codestr(code);
        StreamRamFile *log = new StreamRamFile(HTTP_BUFFER_SIZE);
        log->format("HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Type: application_json\r\n\r\n", code, return_str);
        log->format(json->render());
        // resp->_index = (size_t)log.getLength();
        resp->BodyContext = log;
        resp->BodyCB = &stream_body;
    }

    void html_response(int code, const char *title, const char *fmt, ...)
    {
        const char *return_str = return_codestr(code);
        StreamRamFile *log = new StreamRamFile(HTTP_BUFFER_SIZE);
        log->format("HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Type: text_html\r\n\r\n", code, return_str);

        va_list ap;
        log->format("<html><body><h1>%s</h1>\n<p>", title);
        va_start(ap, fmt);
        log->format_ap(fmt, ap);
        va_end(ap);

        log->format("</p></body></html>\r\n\r\n");
        // resp->_index = (size_t)log.getLength();
        resp->BodyContext = log;
        resp->BodyCB = &stream_body;
    }

    void error(const char *fmt, ...)
    {
        // TODO should be dynamic. Maybe mstring should have a format function!
        char msg[200];
        va_list ap;
        va_start(ap, fmt);
        vsprintf(msg, fmt, ap);
        va_end(ap);
        errors->add(msg);
    }
};

class ArgsURI : public Args
{
    UrlComponents comps;
    IndexedList<const char *> *temporaries;
public:
    ArgsURI() : Args()
    {
        memset(&comps, 0, sizeof(comps));
        temporaries = NULL;
    }

    virtual ~ArgsURI()
    {
        ClearAll();
    }

    void ClearAll()
    {
        // clear temporaries
        if (temporaries) {
            for(int i=0; i<temporaries->get_elements(); i++) {
                delete (*temporaries)[i];
            }
            delete temporaries;
        }
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

    void temporary(const char *im_trash)
    {
        if (!temporaries) {
            temporaries = new IndexedList<const char *>(2, NULL);
        }
        temporaries->append(im_trash);
    }

    static const ApiCall_t *find_api_call(HTTPMethod method, const char *route, const char *command)
    {
        IndexedList<const ApiCall_t *> *commandTable = (*getRoutesList())[route];
        if (!commandTable) {
            printf("Unknown route %s\n", route);
            return NULL;
        }
        for (int i = 0; i < commandTable->get_elements(); i++) {
            const ApiCall_t *t = (*commandTable)[i];
            if (t->method != method) {
                continue;
            }
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

        return find_api_call(hdr->Method, comps.route, comps.command);
    }

    const char *get_path(void)
    {
        return get_unnamed_arg(0);
    }

    int get_int(const char *key, int alt)
    {
        const char *str = get_or(key, NULL);
        if (!str) {
            return alt;
        }
        return strtol(str, NULL, 0);
    }

    int Validate(const ApiCall_t& def, ResponseWrapper *resp)
    {
        int errors = 0;
        // checks if all elements from the dictionary are
        // actually valid parameters
        uint32_t matched = 0;
        for (int i=0; i < get_elements(); i++) {
            const char *k = get_key(i);
            const Param_t *p = def.params;
            bool found = false;
            for(int j=0; j < def.param_count; j++) {
                if (strcmp(k, p[j].long_param) == 0) {
                    matched |= (1 << j);
                    found = true;
                    break;
                }
            }
            if(!found) {
                resp->error("Function %s does not have parameter %s", def.cmd, k);
                errors ++;
            }
        }

        // Now check if all required arguments are actually given
        const Param_t *p = def.params;
        for(int i=0; i < def.param_count; i++) {
            if (!(matched & (1 << i))) {
                if(p[i].flags & P_REQUIRED) {
                    resp->error("Function %s requires parameter %s", def.cmd, p[i].long_param);
                    errors ++;
                }
            }
        }

        return errors;
    }
};


#define MCONC(A,B) A##B

#define API_CALL(VERB, ROUTE, COMMAND, BODYSETUP, PARAMS)                                                              \
    static void Do_##VERB##_##ROUTE##_##COMMAND(ArgsURI &args, HTTPReqMessage *req, ResponseWrapper *resp,             \
                                                void *body);                                                           \
    const Param_t c_params_##VERB##_##ROUTE##_##COMMAND[] = PARAMS;                                                    \
    const ApiCall_t http_##VERB##_##ROUTE##_##COMMAND = {                                                              \
        ((HTTPMethod)HTTP_##VERB),                                                                                     \
        ((const char *)#ROUTE),                                                                                        \
        ((const char *)#COMMAND),                                                                                      \
        ((APIFUNC)Do_##VERB##_##ROUTE##_##COMMAND),                                                                    \
        ((BodyHandlerSetupFunc_t)BODYSETUP),                                                                           \
        ((const int)sizeof(c_params_##VERB##_##ROUTE##_##COMMAND)/sizeof(Param_t)),                                    \
        ((const Param_t *)c_params_##VERB##_##ROUTE##_##COMMAND),                                                      \
    };                                                                                                                 \
    ApiCallRegistrar RegisterApiCall_##VERB##_##ROUTE##_##COMMAND(&http_##VERB##_##ROUTE##_##COMMAND);                 \
    static void Do_##VERB##_##ROUTE##_##COMMAND(ArgsURI &args, HTTPReqMessage *req, ResponseWrapper *resp, void *body)

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
