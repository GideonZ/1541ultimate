#ifndef ASSEMBLY_H
#define ASSEMBLY_H

#include "json.h"
#include "http_request.h"

class Assembly
{
    t_BufferedBody *body;
    JSON *presets;
    int socket_fd;
    HTTPReqMessage response;

    int   connect_to_server(void);
    void  close_connection(void);
public:
    Assembly() {
        body = NULL;
        presets = NULL;
        socket_fd = -1;
    }
    ~Assembly() {
        if (presets)
            delete presets;
        if (body)
            delete body;
    }
    void *get_user_context() { return response.userContext; }
    JSON *get_presets(void);
    JSON *send_query(const char *query);
    JSON *request_entries(const char *id, int cat);
    void  request_binary(const char *id, int cat, int idx);
    void  request_binary(const char *path, const char *filename);
};

extern Assembly assembly;

#endif // ASSEMBLY_H