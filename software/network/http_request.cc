//#include "attachment_writer.h"
#include "http_request.h"
#include "netdb.h"
#include <errno.h>

int HttpRequest :: connect_to_server(const char *hostname, uint16_t hostport)
{
    int error;
    struct hostent my_host, *ret_host;
    struct sockaddr_in serv_addr;
    char buffer[1024];

    printf("Connect to %s:%d\n", hostname, hostport);

    this->socket_fd = -1;
    InitReqMessage(&this->response);
    this->response.usedAsResponseFromServer = 1;

    // setup the connection
    int result = gethostbyname_r(hostname, &my_host, buffer, 1024, &ret_host, &error);
    if (result) {
        printf("Result Get HostName: %d\n", result);
    }

    if (!ret_host) {
        printf("Could not resolve host '%s'.\n", hostname);
        return -1;
    }

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        printf("NO socket\n");
        return sock_fd;
    }

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, ret_host->h_addr, ret_host->h_length);
    serv_addr.sin_port = htons(hostport);

    if (connect(sock_fd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
        printf("Connection failed.\n");
        close(sock_fd);
        return -1;
    }
    // printf("Connection succeeded.\n");
    this->socket_fd = sock_fd;
    return sock_fd;
}

int HttpRequest :: send_request(StreamRamFile *s)
{
    int n = 0;
    int r;
    char buffer[128];
    do {
        n = s->read(buffer, 128);
        if (n) {
            r = send(socket_fd, buffer, n, MSG_DONTWAIT);
            if (r < 0) {
                return r;
            }
        }
    } while(n);
    return 0;
}

bool HttpRequest :: recv_response(void)
{
    bool complete = get_response(this->socket_fd, collect_in_buffer, response);
    this->body = (t_BufferedBody *)response.userContext;
    if (!complete || !this->body) {
        delete this->body;
        this->body = NULL;
        response.userContext = NULL;
        return false;
    }
    this->body->offset = 0;
    return true;
}

void attachment_to_buffer(BodyDataBlock_t *block)
{
    HTTPHeaderField *f;
    t_BufferedBody *body = (t_BufferedBody *)block->context;

    switch(block->type) {
        case eStart:
            // printf("--- Start of Body --- (Type: %s)\n", block->data);
            break;
        case eDataStart:
            // printf("--- Raw Data Start ---\n");
            body->offset = 0;
            body->size = 0;
            break;
        case eSubHeader:
            // printf("--- SubHeader ---\n");
            f = (HTTPHeaderField *)block->data;
            for(int i=0; i < block->length; i++) {
                printf("%s => '%s'\n", f[i].key, f[i].value);
            }
            break;
        case eDataBlock:
            // printf("--- Data (%d bytes)\n", block->length);
            if (block->length < (16384 - body->offset)) {
                memcpy(body->buffer + body->offset, block->data, block->length);
                body->offset += block->length;
                body->size += block->length;
            } else {
                printf("-> Ditched, buffer full.\n");
            }
            break;
        case eDataEnd:
            // printf("--- End of Data ---\n");
            break;
        case eTerminate:
            printf("--- End of Body --- ");
            printf("Total size: %d\n", body->size);
            break;
    }
}

void collect_in_buffer(HTTPReqMessage *req, HTTPRespMessage *resp)
{
    t_BufferedBody *body = new t_BufferedBody;
    req->userContext = body;
    body->offset = 0;
    body->size = 0;
    setup_multipart(req, &attachment_to_buffer, body);
}

int read_socket(int socket_fd, HTTPReqMessage& response)
{
    char *p = (char *)response._buf;
    p += response._valid;
    int space = HTTP_BUFFER_SIZE - response._valid;
    printf(".");
    int n = space ? recv(socket_fd, p, space, 0) : 0;
    if (n >= 0) {
        response._valid += n;
    }
    return n;
}

bool get_response(int socket_fd, HTTPREQ_CALLBACK callback, HTTPReqMessage& response)
{
    uint8_t state = READING_SOCKET;

    do {
        int n = read_socket(socket_fd, response);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        state = ProcessClientData(&response, NULL, callback);
    } while(state < WRITING_SOCKET);

    // Completed bodies release their absorber in ProcessClientData. Abort any
    // remaining absorber on early EOF, read failure, or a bodyless response.
    if (response.BodyCB) {
        response.BodyCB(response.BodyContext, NULL, -1);
        response.BodyCB = NULL;
        response.BodyContext = NULL;
    }
    return state >= WRITING_SOCKET && response.protocol_state != eReq_HeaderTooBig;
}

JSON *convert_buffer_to_json(t_BufferedBody *body)
{
    body->buffer[body->size] = 0;
    JSON *json = NULL;
    int j = convert_text_to_json_objects((char *)body->buffer, body->size, 1000, &json);
    if (j < 0) {
        if (json) {
            delete json;
            return NULL;
        }
    }
    return json;
}
