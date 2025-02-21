#ifndef ROUTES_H
#define ROUTES_H

#include "cli.h"
#include "indexed_list.h"
#include "stream_textlog.h"
#include "stream_ramfile.h"
#include "json.h"
#include "http_codes.h"
#include "pattern.h"
#include "network_config.h"

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
    StreamRamFile *attachment;

    ResponseWrapper(HTTPRespMessage *resp) : resp(resp) {
        json = JSON::Obj();
        errors = JSON::List();
        attachment = NULL;
    }

    ~ResponseWrapper() {
        delete json;
        if (errors) {
            delete errors;
        }
        if (attachment) {
            delete attachment;
        }
    }

    StreamRamFile *add_attachment(void) {
        if (!attachment) {
            attachment = new StreamRamFile(1024);
        }
        return attachment;
    }

    const char *return_codestr(int code) {
        const char *reply = (code == 200)   ? "OK"
                            : (code == 203) ? "No Content"
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

        // First, the body data is prepared
        StreamRamFile *log = new StreamRamFile(HTTP_BUFFER_SIZE);
        json->render(log);
        resp->BodyContext = log;
        resp->BodyCB = &stream_body;

        // Then the header is made, based on the size of the body
        StreamTextLog *hdr = new StreamTextLog(HTTP_BUFFER_SIZE, (char *)resp->_buf);
        const char *return_str = return_codestr(code);
        hdr->format("HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Type: application/json\r\n", code, return_str);
        hdr->format("Content-Length: %d\r\n\r\n", log->getLength());
        resp->_index = hdr->getLength();
        delete hdr; // no longer needed
    }

    void binary_response(void)
    {
        StreamTextLog *hdr = new StreamTextLog(HTTP_BUFFER_SIZE, (char *)resp->_buf);
        if (attachment) {
            hdr->format("HTTP/1.1 200 OK\r\n");
            hdr->format("Content-Type: application/octet-stream\r\n");
            const char *fn = attachment->getFileName();
            if (fn) {
                hdr->format("Content-Disposition: attachment; filename=\"%s\"\r\n", fn);
            } else {
                hdr->format("Content-Disposition: attachment\r\n");
            }
            hdr->format("Content-Length: %d\r\n", attachment->getLength());
            resp->BodyContext = attachment;
            resp->BodyCB = &stream_body;
            attachment = NULL; // now owned by BodyCB
        } else {
            hdr->format("HTTP/1.1 203 No Content\r\n");
        }
        hdr->format("Connection: close\r\n\r\n");
        resp->_index = hdr->getLength();
        delete hdr; // no longer needed
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
        if (!fmt) {
            return;
        }
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
    int path_depth;
    char *path_parts[16];
    char *path_copy;
    mstring full_path;
    IndexedList<const char *> *temporaries;

    void store_path(const char *fp)
    {
        path_depth = 0;
        full_path = "";
        if (!fp) {
            return;
        }
        if (strlen(fp) == 0) {
            return;
        }
        if (!path_copy) {
            path_copy = strdup(fp);
            path_depth = split_string('/', path_copy, path_parts, 16);
            for (int i = 0; i < path_depth; i++) {
                url_decode(path_parts[i], path_parts[i], 0);
                if (i != 0) {
                    full_path += "/";
                }
                full_path += path_parts[i];
            }
        }
    }

public:
    ArgsURI() : Args(), full_path()
    {
        memset(&comps, 0, sizeof(comps));
        temporaries = NULL;
        path_copy = NULL;
        path_depth = 0;
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
            temporaries = NULL;
        }
        if (path_copy) {
            free(path_copy);
            path_copy = NULL;
            path_depth = 0;
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
            url_decode(name, name, 0);
            if (value == NULL) {
                set(name, "");
            } else {
                url_decode(value, value, 0);
                set(name, value);
            }
        }
        printf("%s:", comps.path);
        store_path(comps.path);
        printf("%s -> %d [", full_path.c_str(), path_depth);
        for (int i=0;i<path_depth;i++) {
            printf("%s, ", path_parts[i]);
        } printf("]\n");

        const ApiCall_t *call = find_api_call(hdr->Method, comps.route, comps.command);

        // Validate password
        const char *password = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
        const char *supplied_password = NULL;
        for(int h=0; h < hdr->FieldCount; ++h) {
            if(strcasecmp(hdr->Fields[h].key, "X-Password") == 0) {
                supplied_password = hdr->Fields[h].value;
            }
        }
        if(call && (*password && strcmp(supplied_password, password) != 0))
            return (ApiCall_t *)-1;  // Signal that endpoint exists but password is incorrect

        return call;
    }

    int get_path_depth(void)
    {
        return path_depth;
    }

    const char *get_full_path()
    {
        return full_path.c_str();
    }

    const char *get_path(int index)
    {
        if ((index < 0) || (index >= path_depth)) {
            return NULL;
        }
        return path_parts[index];
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

class TempfileWriter;
TempfileWriter *attachment_writer(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args);


#endif // ROUTES_H
