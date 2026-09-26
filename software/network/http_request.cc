//#include "attachment_writer.h"
#include "http_request.h"
#include "netdb.h"

static HttpSecureConnectionFactory secure_factory = NULL;

void http_set_secure_connection_factory(HttpSecureConnectionFactory factory)
{
    secure_factory = factory;
}

int HttpRequest :: connect_to_server(const char *hostname, uint16_t hostport, bool secure)
{
    int error;
    struct hostent my_host, *ret_host = NULL;
    struct sockaddr_in serv_addr;
    char buffer[1024];

    printf("Connect to %s:%d\n", hostname, hostport);

    if (socket_fd >= 0 || secure_connection) return -1;
    InitReqMessage(&this->response);
    this->response.usedAsResponseFromServer = HTTP_RESPONSE_STRICT;

    if (secure) {
        if (!secure_factory) return -1;
        secure_connection = secure_factory();
        if (!secure_connection) return -1;
        return secure_connection->open(hostname, hostport);
    }

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
            int sent = 0;
            while (sent < n) {
                r = secure_connection ? secure_connection->write(buffer + sent, n - sent)
                                      : send(socket_fd, buffer + sent, n - sent, MSG_DONTWAIT);
                if (r <= 0 || r > n - sent) return fail("http_write", r);
                sent += r;
            }
        }
    } while(n);
    return 0;
}

int HttpRequest :: recv_response(void)
{
    uint8_t state = READING_SOCKET;
    while (state < WRITING_SOCKET) {
        int space = HTTP_BUFFER_SIZE - response._valid;
        if (space <= 0) return fail("http_buffer_full", space);
        int n = secure_connection ? secure_connection->read(response._buf + response._valid, space)
                                  : recv(socket_fd, response._buf + response._valid, space, 0);
        if (n == 0 && body && response.protocol_state == eReq_Body &&
            response.bodyType == eUntilDisconnect && response.BodyCB) {
            response.BodyCB(response.BodyContext, NULL, 0);
            response.BodyCB = NULL;
            response.BodyContext = NULL;
            break;
        }
        if (n <= 0 || n > space) return fail("http_read", n);
        response._valid += n;
        state = ProcessClientData(&response, NULL, collect_in_buffer);
        body = (t_BufferedBody *)response.userContext;
        if (body && body->overflow) return fail("http_body_overflow", body->size);
    }
    if (!body || body->overflow || response.protocol_state == eReq_HeaderTooBig ||
        (response.BodyCB && response.bodyType != eNoBody)) return fail("http_framing", state);
    body->offset = 0;
    return 0;
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
                body->overflow = true;
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
    if (!body) return;
    body->offset = 0;
    body->size = 0;
    body->overflow = false;
    setup_multipart(req, &attachment_to_buffer, body);
    if (!req->BodyCB) body->overflow = true; // allocation failure must not look like an empty response
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

void get_response(int socket_fd, HTTPREQ_CALLBACK callback, HTTPReqMessage& response)
{
    uint8_t state = WRITING_SOCKET;
    
    do {
        int n = read_socket(socket_fd, response);
        if (n) {
            state = ProcessClientData(&response, NULL, callback);
        }
    } while(state < WRITING_SOCKET);
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
