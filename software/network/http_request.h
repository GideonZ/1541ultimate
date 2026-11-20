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
    HTTPReqMessage *response;
    bool failed;
    uint8_t buffer[16384 + 1]; // Payload capacity plus a separate string terminator.
} t_BufferedBody;

// Callbacks
void collect_in_buffer(HTTPReqMessage *req, HTTPRespMessage *resp);
//void write_to_temp(HTTPReqMessage *req, HTTPRespMessage *resp);

int   read_socket(int socket_fd, HTTPReqMessage& response);
bool  get_response(int socket_fd, HTTPREQ_CALLBACK callback, HTTPReqMessage& response);
JSON *convert_buffer_to_json(t_BufferedBody *body);

// How long an exchange waits for the server to take more of the request, or to
// send more of its response, before it gives up. The host tests set it lower.
#ifndef HTTP_REQUEST_TIMEOUT_MS
#define HTTP_REQUEST_TIMEOUT_MS 30000
#endif

#include "socket_slices.h"

// No heap allocation is needed for the default HTTP transport. A descriptor
// supplied to the constructor is borrowed by the legacy parser helpers.
class HttpSocketConnection : public HttpConnection
{
    int socket_fd;
    SliceWait *wait;
    HttpSocketConnection(const HttpSocketConnection&);
    HttpSocketConnection& operator=(const HttpSocketConnection&);
public:
    explicit HttpSocketConnection(int fd = -1, SliceWait *slices = NULL) : socket_fd(fd), wait(slices) {}
    int open(const char *hostname, uint16_t port);
    int read(void *buffer, int length);
    int write(const void *buffer, int length);
    void close_session();
};

class HttpRequest
{
    t_BufferedBody *body;
    HTTPReqMessage response;
    SliceWait wait;
    HttpSocketConnection socket_connection;
    HttpConnection *connection;
    enum ConnectionState { Idle, Connected, Failed } connection_state;
    HttpRequest(const HttpRequest&);
    HttpRequest& operator=(const HttpRequest&);
    int fail_connection();
public:
    // A supplied backend is borrowed and must outlive this request. The request
    // owns its session: failures and destruction close it, but never delete it.
    explicit HttpRequest(HttpConnection *transport = NULL, volatile bool *cancel = NULL) :
        wait(cancel, HTTP_REQUEST_TIMEOUT_MS), socket_connection(-1, &wait),
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
    void detach() { wait.detach(); }
    void disconnect() {
        if (connection_state == Connected) connection->close_session();
        connection_state = Failed;
    }
    bool recv_response(void);

    HTTPReqHeader *get_header(void) {
        return &response.Header;
    }

    char *get_response_string(void) {
        if (!body) {
            return NULL;
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
