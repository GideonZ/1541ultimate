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

// An HttpRequest waits on its socket in slices of this length, so that it sees
// a cancel within one slice.
#define HTTP_IO_SLICE_MS 100

// Stops an exchange when it is cancelled or when it has made no progress for
// HTTP_REQUEST_TIMEOUT_MS. The time is measured, not counted in slices: on lwIP a
// send slice lasts about a second, not HTTP_IO_SLICE_MS. There is no limit on the
// exchange as a whole: a slow peer, like the printer in #1039, may take as long as
// it needs while it moves.
static inline uint32_t http_now_ms(void)
{
#ifdef RUNS_ON_PC
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
#else
    return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
#endif
}

class HttpWait
{
    volatile bool *cancel;
    uint32_t last_progress;
public:
    HttpWait(volatile bool *c) : cancel(c), last_progress(http_now_ms()) { }
    void progress(void) { last_progress = http_now_ms(); }
    bool cancelled(void) { return cancel && *cancel; }
    // The cancel flag belongs to the job, which can go before the exchange does.
    void detach(void) { cancel = NULL; }
    // After a slice that timed out: whether to wait another one.
    bool again(void)
    {
        return !cancelled() && ((uint32_t)(http_now_ms() - last_progress) < HTTP_REQUEST_TIMEOUT_MS);
    }
};

int   read_socket(int socket_fd, HTTPReqMessage& response);
bool  get_response(int socket_fd, HTTPREQ_CALLBACK callback, HTTPReqMessage& response, HttpWait *wait = NULL);
JSON *convert_buffer_to_json(t_BufferedBody *body);

class HttpRequest
{
    t_BufferedBody *body;
    HTTPReqMessage response;
    int socket_fd;
    HttpWait wait;
public:
    // A request whose *cancel becomes true stops within HTTP_IO_SLICE_MS, except
    // while it looks up the host name.
    HttpRequest(volatile bool *cancel = NULL) : wait(cancel) {
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
