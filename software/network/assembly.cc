#include "assembly.h"
#include <sys/socket.h>
#include "netdb.h"
#include "attachment_writer.h"

#define HOSTNAME      "hackerswithstyle.se"
#define HOSTPORT      80
#define URL_SEARCH    "/leet/search/aql?query="
#define URL_PATTERNS  "/leet/search/aql/presets"
#define URL_ENTRIES   "/leet/search/entries"
#define URL_DOWNLOAD  "/leet/search/bin"

Assembly assembly;

void url_encode(const char *src, mstring &dest)
{
    int len = strlen(src);
    char pct[4] = {0};

    for(int i=0; i<len; i++) {
        if (src[i] == ' ') {
            dest += '+';
        } else if(src[i] == '_' || src[i] == '-' || src[i] == '.' || src[i] == '*') {
            dest += src[i];
        } else if(src[i] >= 'a' && src[i] <= 'z') {
            dest += src[i];
        } else if(src[i] >= 'A' && src[i] <= 'Z') {
            dest += src[i];
        } else if(src[i] >= '0' && src[i] <= '9') {
            dest += src[i];
        } else {
            sprintf(pct, "%c%02x", '%', src[i]);
            dest += pct;
        }
    }
}

void attachment_to_buffer(BodyDataBlock_t *block)
{
    HTTPHeaderField *f;
    t_BufferedBody *body = (t_BufferedBody *)block->context;

    switch(block->type) {
        case eStart:
            printf("--- Start of Body --- (Type: %s)\n", block->data);
            break;
        case eDataStart:
            printf("--- Raw Data Start ---\n");
            body->offset = 0;
            body->size = 0;
            break;
        case eSubHeader:
            printf("--- SubHeader ---\n");
            f = (HTTPHeaderField *)block->data;
            for(int i=0; i < block->length; i++) {
                printf("%s => '%s'\n", f[i].key, f[i].value);
            }
            break;
        case eDataBlock:
            printf("--- Data (%d bytes)\n", block->length);
            if (block->length < (16384 - body->offset)) {
                memcpy(body->buffer + body->offset, block->data, block->length);
                body->offset += block->length;
                body->size += block->length;
            } else {
                printf("-> Ditched, buffer full.\n");
            }
            break;
        case eDataEnd:
            printf("--- End of Data ---\n");
            break;
        case eTerminate:
            printf("--- End of Body ---\n");
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

void write_to_temp(HTTPReqMessage *req, HTTPRespMessage *resp)
{
    TempfileWriter *writer = new TempfileWriter(req, resp, NULL, NULL, NULL);
    setup_multipart(req, &TempfileWriter::collect_wrapper, writer);
    req->userContext = writer;
}

int Assembly :: connect_to_server(void)
{
    int error;
    struct hostent my_host, *ret_host;
    struct sockaddr_in serv_addr;
    char buffer[1024];

    this->socket_fd = 0;
    InitReqMessage(&this->response);

    // setup the connection
    int result = gethostbyname_r(HOSTNAME, &my_host, buffer, 1024, &ret_host, &error);
    printf("Result Get HostName: %d\n", result);

    if (!ret_host) {
        printf("Could not resolve host '%s'.\n", HOSTNAME);
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
    serv_addr.sin_port = htons(HOSTPORT);

    if (connect(sock_fd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
        printf("Connection failed.\n");
        return -1;
    }
    printf("Connection succeeded.\n");
    this->socket_fd = sock_fd;
    return sock_fd;
}

void Assembly :: close_connection(void)
{
    if(socket_fd) {
        close(socket_fd);
    }
    socket_fd = 0;
}

JSON *Assembly :: send_query(const char *query)
{
    mstring encoded;
    url_encode(query, encoded);

    mstring request("GET " URL_SEARCH);
    request += encoded.c_str();
    request += " HTTP/1.1\r\n"
        "Accept-encoding: identity\r\n"
        "Host: " HOSTNAME "\r\n"
        "User-Agent: Ultimate\r\n"
        "Connection: close\r\n"
        "\r\n";

    int n = send(this->socket_fd, request.c_str(), request.length(), MSG_DONTWAIT);
    get_response(collect_in_buffer);
    
    t_BufferedBody *body = (t_BufferedBody *) response.userContext;
    JSON *json = convert_buffer_to_json(body);
//    delete body;    
    return json;
}

JSON *Assembly :: request_entries(const char *id, int cat)
{
    mstring enc_id;
    url_encode(id, enc_id);

    char buffer[64];
    sprintf(buffer, "GET " URL_ENTRIES "/%s/%d", enc_id.c_str(), cat);
    mstring request(buffer);
    request += " HTTP/1.1\r\n"
        "Accept-encoding: identity\r\n"
        "Host: " HOSTNAME "\r\n"
        "User-Agent: Ultimate\r\n"
        "Connection: close\r\n"
        "\r\n";

    int n = send(this->socket_fd, request.c_str(), request.length(), MSG_DONTWAIT);
    get_response(collect_in_buffer);

    t_BufferedBody *body = (t_BufferedBody *) response.userContext;
    JSON *json = convert_buffer_to_json(body);
//    delete body;    
    return json;
}

void Assembly :: request_binary(const char *id, int cat, int idx)
{
    mstring enc_id;
    url_encode(id, enc_id);

    char buffer[64];
    sprintf(buffer, "GET " URL_DOWNLOAD "/%s/%d/%d", enc_id.c_str(), cat, idx);
    mstring request(buffer);
    request += " HTTP/1.1\r\n"
        "Accept-encoding: identity\r\n"
        "Host: " HOSTNAME "\r\n"
        "User-Agent: Ultimate\r\n"
        "Connection: close\r\n"
        "\r\n";

    int n = send(this->socket_fd, request.c_str(), request.length(), MSG_DONTWAIT);
    get_response(write_to_temp);
 }

int Assembly :: read_socket(void)
{
    char *p = (char *)response._buf;
    p += response._valid;
    int space = HTTP_BUFFER_SIZE - response._valid;
    printf(".");
    int n = space ? recv(this->socket_fd, p, space, 0) : 0;
    if (n >= 0) {
        response._valid += n;
    }
    return n;
}

void Assembly :: get_response(HTTPREQ_CALLBACK callback)
{
    uint8_t state = WRITING_SOCKET;
    
    do {
        int n = read_socket();
        if (n) {
            state = ProcessClientData(&response, NULL, callback);
        }
    } while(state < WRITING_SOCKET);
}

JSON *Assembly :: convert_buffer_to_json(t_BufferedBody *body)
{
    body->buffer[body->size] = 0;
    JSON *json = NULL;
    int j = convert_text_to_json_objects((char *)body->buffer, body->size, 1000, &json);
    return json;
}

#if TEST
int TempfileWriter::temp_count;

extern "C" {
    void outbyte(int c) { putc(c, stdout); }
}

int main(int argc, char **argv)
{

    int sock = assembly.connect_to_server();
    if (sock >= 0) {
        JSON *json = assembly.request_entries("237033", 0);
        assembly.close_connection();
        puts(json->render());
        t_BufferedBody *body = (t_BufferedBody *) assembly.get_user_context();
        delete body;
        delete json;
    }

    int sock2 = assembly.connect_to_server();
    if (sock2 >= 0) {
        assembly.request_binary("237033", 0, 0);
        assembly.close_connection();
        TempfileWriter *writer = (TempfileWriter *) assembly.get_user_context();
        delete writer;
    }
}
#endif
