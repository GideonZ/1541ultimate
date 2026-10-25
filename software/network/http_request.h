#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <sys/socket.h>
#include "http_connection.h"
extern "C" {
    #include "server.h"
    #include "multipart.h"
}
#include "json.h"

typedef struct {
    int offset;
    int size;
    uint8_t buffer[16384];
} t_BufferedBody;

// Callbacks
void collect_in_buffer(HTTPReqMessage *req, HTTPRespMessage *resp);
//void write_to_temp(HTTPReqMessage *req, HTTPRespMessage *resp);

int   read_socket(int socket_fd, HTTPReqMessage& response);
bool  get_response(int socket_fd, HTTPREQ_CALLBACK callback, HTTPReqMessage& response);
JSON *convert_buffer_to_json(t_BufferedBody *body);

class HttpRequest
{
    t_BufferedBody *body;
    HTTPReqMessage response;
    HttpSocketConnection socket_connection;
    HttpConnection *connection;
    enum ConnectionState { Idle, Connected, Failed } connection_state;
    HttpRequest(const HttpRequest&);
    HttpRequest& operator=(const HttpRequest&);
    int fail_connection();
public:
    // A supplied backend is borrowed and must outlive this request. The request
    // owns its session: failures and destruction close it, but never delete it.
    explicit HttpRequest(HttpConnection *transport = NULL) :
        connection(transport ? transport : &socket_connection), connection_state(Idle) {
        InitReqMessage(&response);
        body = NULL;
    }
    ~HttpRequest() {
        if (connection_state == Connected) {
            connection->close_session();
        }
        delete body;
    }
    int connect_to_server(const char *hostname, uint16_t hostport);
    int send_request(StreamRamFile *req);
    bool recv_response(void);

    HTTPReqHeader *get_header(void) {
        return &response.Header;
    }

    char *get_response_string(void) {
        if (!body) {
            return NULL;
        }
        if (body->size > 16383) {
            body->size = 16383;
        }
        body->buffer[body->size] = 0;
        return (char *)body->buffer;
    }

    JSON *get_json(void) {
        if(body) {
            return convert_buffer_to_json(body);
        }
        return NULL;
    }

    int read_response_data(int size, uint8_t *dest) {
        printf("%p %d %d\n", body, (body)?body->offset:0, (body)?body->size:0);
        if (!body || (body->offset >= body->size)) {
            return 0;
        }
        int avail = (body->size - body->offset);
        int now = (avail < size) ? avail : size;
        memcpy(dest, body->buffer + body->offset, now);
        body->offset += now;
        return now;
    }
};


#endif
