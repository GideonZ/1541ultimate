#include "assembly.h"
#include <sys/socket.h>
#include "netdb.h"
#include "attachment_writer.h"
#include "u64.h"

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
        if(src[i] == '_' || src[i] == '-' || src[i] == '.' || src[i] == '*') {
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

    this->socket_fd = -1;
    InitReqMessage(&this->response);
    this->response.usedAsResponseFromServer = 1;

#if U64==2    
    if (!(U64II_BLACKBOARD & 1)) {
        return -1;
    }
#endif

    // setup the connection
    int result = gethostbyname_r(HOSTNAME, &my_host, buffer, 1024, &ret_host, &error);
    if (result) {
        printf("Result Get HostName: %d\n", result);
    }

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
    // printf("Connection succeeded.\n");
    this->socket_fd = sock_fd;
    return sock_fd;
}

void Assembly :: close_connection(void)
{
    if(socket_fd >= 0) {
        close(socket_fd);
    }
    socket_fd = -1;
}

JSON *Assembly :: get_presets(void)
{
    static const char request[] = 
        "GET " URL_PATTERNS " HTTP/1.1\r\n"
        "Accept-encoding: identity\r\n"
        "Host: " HOSTNAME "\r\n"
        "User-Agent: Assembly Query\r\n"
        "Client-Id: Ultimate\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (presets) {
        return presets;
    }
    if (connect_to_server() >= 0) {
        send(this->socket_fd, request, strlen(request), MSG_DONTWAIT);
        get_response(collect_in_buffer);
        close_connection();

        body = (t_BufferedBody *) response.userContext;
        presets = convert_buffer_to_json(body);
        return presets;
    }
    return NULL;
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
        "User-Agent: Assembly Query\r\n"
        "Client-Id: Ultimate\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (connect_to_server() >= 0) {
        send(this->socket_fd, request.c_str(), request.length(), MSG_DONTWAIT);
        get_response(collect_in_buffer);
        close_connection();

        t_BufferedBody *body = (t_BufferedBody *) response.userContext;
        JSON *json = convert_buffer_to_json(body);
        return json;
    }
    return NULL;
}

/* Example result from query:
[ 
{   "name" : "Jumpman Junior",  "id" : "237033",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "Wattenscheider Cracking Service",  "year" : 0,  "rating" : 0,  "updated" : "2023-11-21"}, 
{   "name" : "Jumpman Junior",  "id" : "232391",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "N.O.W.S.E",  "year" : 1989,  "rating" : 0,  "updated" : "2023-05-11",  "released" : "1989-01-01"}, 
{   "name" : "Jumpman +",  "id" : "209120",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "Cracker Force Nijmegen",  "year" : 0,  "rating" : 0,  "updated" : "2021-09-22"}, 
{   "name" : "Jumpman Junior +",  "id" : "208821",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "Cracker Force Nijmegen",  "year" : 0,  "rating" : 0,  "updated" : "2021-09-20"}, 
{   "name" : "Jumpman Junior +",  "id" : "204685",  "category" : 7,  "siteCategory" : 17,  "siteRating" : 0,  "group" : "The Sir",  "year" : 0,  "rating" : 0,  "updated" : "2021-05-25"}, 
{   "name" : "Jumpman Junior +",  "id" : "200855",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "Lion Heart",  "year" : 1985,  "rating" : 0,  "updated" : "2021-03-06",  "released" : "1985-01-01"}, 
{   "name" : "Jumpman Junior +",  "id" : "200276",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "RCS International",  "year" : 1987,  "handle" : "Tasco",  "rating" : 0,  "updated" : "2021-02-20",  "released" : "1987-06-16"}, 
{   "name" : "Jumpman Junior +",  "id" : "199939",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "The Ultimate Mayas",  "year" : 0,  "handle" : "Chico",  "rating" : 0,  "updated" : "2021-02-14"}, 
{   "name" : "Jumpman Junior",  "id" : "194399",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "Feuerteufel",  "year" : 0,  "rating" : 0,  "updated" : "2020-08-10"}, 
{   "name" : "Jumpman Junior",  "id" : "193761",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "Mr. 99",  "year" : 1984,  "rating" : 0,  "updated" : "2020-07-25",  "released" : "1984-01-01"}, 
{   "name" : "Jumpman Junior",  "id" : "193188",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "year" : 1984,  "rating" : 0,  "updated" : "2020-07-06",  "released" : "1984-01-01"}, 
{   "name" : "Jumpman +",  "id" : "185672",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "Hulkamania",  "year" : 0,  "handle" : "Hibisch",  "rating" : 0,  "updated" : "2020-01-01"}, 
{   "name" : "Jumpman Junior +",  "id" : "173866",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "Greg",  "year" : 0,  "rating" : 0,  "updated" : "2019-01-12"}, 
{   "name" : "Jumpman Junior Extended",  "id" : "168857",  "category" : 0,  "siteCategory" : 20,  "siteRating" : 0,  "group" : "Oleander",  "year" : 1984,  "rating" : 0,  "updated" : "2018-09-25",  "released" : "1984-01-01"}, 
{   "name" : "Jump Man BASIC",  "id" : "27487",  "category" : 16,  "siteCategory" : 0,  "siteRating" : 0,  "year" : 0,  "rating" : 0,  "updated" : "2018-09-08"}, 
{   "name" : "Jumpman",  "id" : "4000",  "category" : 16,  "siteCategory" : 0,  "siteRating" : 0,  "year" : 0,  "rating" : 0,  "updated" : "2018-09-08"}, 
{   "name" : "Jumpman Junior",  "id" : "4002",  "category" : 16,  "siteCategory" : 0,  "siteRating" : 0,  "year" : 0,  "rating" : 0,  "updated" : "2018-09-08"}, 
{   "name" : "Jumpman II",  "id" : "17360",  "category" : 16,  "siteCategory" : 0,  "siteRating" : 0,  "year" : 0,  "rating" : 0,  "updated" : "2018-09-08"}, 
{   "name" : "jumpman",  "id" : "1293",  "category" : 15,  "siteCategory" : 0,  "siteRating" : 0,  "group" : "epyx",  "year" : 0,  "rating" : 0,  "updated" : "2017-09-13"}, 
{   "name" : "jumpman junior",  "id" : "1294",  "category" : 15,  "siteCategory" : 0,  "siteRating" : 0,  "group" : "epyx",  "year" : 0,  "rating" : 0,  "updated" : "2017-09-13"} ]
*/

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
        "User-Agent: Assembly Query\r\n"
        "Client-Id: Ultimate\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (connect_to_server() >= 0) {
        send(this->socket_fd, request.c_str(), request.length(), MSG_DONTWAIT);
        get_response(collect_in_buffer);
        close_connection();

        t_BufferedBody *body = (t_BufferedBody *) response.userContext;
        JSON *json = convert_buffer_to_json(body);
        return json;
    }
    return NULL;
}

/* Example result: entries (object)

{ 
  "contentEntry" : [ { 
  "path" : "jumpmanjunior-wcs.d64",
  "id" : 0
} ],
  "isContentByItself" : false
}

*/
void Assembly :: request_binary(const char *id, int cat, int idx)
{
    mstring enc_id;
    url_encode(id, enc_id);

    char buffer[64];
    sprintf(buffer, "/%s/%d/%d", enc_id.c_str(), cat, idx);
    request_binary(buffer, NULL);
}

void Assembly :: request_binary(const char *path, const char *filename)
{
    mstring request("GET " URL_DOWNLOAD);
    request += path;
    if(filename) {
        request += "/";
        url_encode(filename, request);
    }
    request += " HTTP/1.1\r\n"
        "Accept-encoding: identity\r\n"
        "Host: " HOSTNAME "\r\n"
        "User-Agent: Assembly Query\r\n"
        "Client-Id: Ultimate\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (connect_to_server() >= 0) { // resets userContext to NULL
        send(this->socket_fd, request.c_str(), request.length(), MSG_DONTWAIT);
        get_response(write_to_temp);
        close_connection();
    }
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
    if (j < 0) {
        if (json) {
            delete json;
            return NULL;
        }
    }
    return json;
}

#if TEST
int TempfileWriter::temp_count;

extern "C" {
    void outbyte(int c) { putc(c, stdout); }
}

int main(int argc, char **argv)
{
    JSON *results = assembly.send_query("(name:\"jumpman\") & (type:prg)");
    if (results) {
        puts(results->render());
        delete results;
    }

    t_BufferedBody *body2 = (t_BufferedBody *) assembly.get_user_context();
    if (body2) {
        delete body2;
    }

    JSON *json = assembly.request_entries("237033", 0);
    if (json) {
        puts(json->render());
        delete json;
    }
    t_BufferedBody *body = (t_BufferedBody *) assembly.get_user_context();
    if (body) {
        delete body;
    }

    JSON *json3 = assembly.request_entries("145050", 3);
    if (json3) {
        puts(json3->render());
        delete json3;
    }
    t_BufferedBody *body3 = (t_BufferedBody *) assembly.get_user_context();
    if (body3) {
        delete body3;
    }

    assembly.request_binary("237033", 0, 0);

    TempfileWriter *writer = (TempfileWriter *) assembly.get_user_context();
    if (writer) {
        printf("Writer file: %s\n", writer->get_filename(0));
        delete writer;
    }

    assembly.request_binary("145050", 3, 0);

    TempfileWriter *writer2 = (TempfileWriter *) assembly.get_user_context();
    if (writer2) {
        printf("Writer file: %s\n", writer2->get_filename(0));
        delete writer2;
    }
}
#endif
