#ifndef ASSEMBLY_H
#define ASSEMBLY_H

#include "json.h"
extern "C" {
    #include "server.h"
    #include "multipart.h"
}

typedef struct {
    int offset;
    int size;
    uint8_t buffer[16384];
} t_BufferedBody;

void url_encode(const char *src, mstring &dest);

class Assembly
{
    t_BufferedBody *body;
    JSON *presets;
    int socket_fd;
    HTTPReqMessage response;

    int   connect_to_server(void);
    int read_socket(void);
    void  get_response(HTTPREQ_CALLBACK callback);
    JSON *convert_buffer_to_json(t_BufferedBody *body);
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