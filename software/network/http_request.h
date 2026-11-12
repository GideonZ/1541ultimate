#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <sys/socket.h>
#include <stdint.h>
#ifdef RUNS_ON_PC
#include <time.h>
#else
#include "FreeRTOS.h"
#include "task.h"
#endif
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

// How long an exchange waits for the server to take more of the request, or to
// send more of its response, before it gives up. The host tests set it lower.
#ifndef HTTP_REQUEST_TIMEOUT_MS
#define HTTP_REQUEST_TIMEOUT_MS 30000
#endif

// An HttpRequest waits on its socket in slices (socket_slices.h), so that it sees a
// cancel within one slice. It gives up when it has made no progress for
// HTTP_REQUEST_TIMEOUT_MS (measured with a clock). There is no limit on the exchange as a whole: a slow
// peer, like the printer in #1039, may take as long as it needs while it moves.
#include "socket_slices.h"

int   read_socket(int socket_fd, HTTPReqMessage& response);
bool  get_response(int socket_fd, HTTPREQ_CALLBACK callback, HTTPReqMessage& response, SliceWait *wait = NULL);
JSON *convert_buffer_to_json(t_BufferedBody *body);

class HttpRequest
{
    t_BufferedBody *body;
    HTTPReqMessage response;
    int socket_fd;
    SliceWait wait;
public:
    // A request whose *cancel becomes true stops within a slice, except while it looks
    // up the host name: SOCKET_SLICE_MS for a connect or a receive, but about a second
    // for a send on lwIP (socket_slices.h).
    HttpRequest(volatile bool *cancel = NULL) : wait(cancel, HTTP_REQUEST_TIMEOUT_MS) {
        InitReqMessage(&response);
        body = NULL;
        socket_fd = -1;
    }
    ~HttpRequest() {
        disconnect();
        if (body) {
            delete body;
        }
    }
    int connect_to_server(const char *hostname, uint16_t hostport);
    int send_request(StreamRamFile *req);
    // After this, the exchange no longer looks at the cancel flag it was given.
    void detach(void) { wait.detach(); }
    bool recv_response(void);
    // Closes the socket; the response stays.
    void disconnect(void) {
        if(socket_fd >= 0) {
            close(socket_fd);
        }
        socket_fd = -1;
    }

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
