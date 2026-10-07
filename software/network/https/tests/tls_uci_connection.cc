/* Host-only UCI integration with the production TLS stream and local TCP.
 * Trust roots are supplied by the test harness, never by firmware requests. */
#include "http_target.h"
#include "tls_stream.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static mbedtls_x509_crt roots;
static unsigned created,destroyed;
static unsigned failed_connections,failed_plain_bytes;
static tls_stream_diagnostics failed_diagnostic;
static int failed_http_code;
static char failed_http_stage[40];
static int64_t monotonic_ms(void *)
{
    timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static bool clock_ready(void *) { return true; }
static int trust_roots(void *,mbedtls_ssl_config *config)
{ mbedtls_ssl_conf_ca_chain(config,&roots,NULL);return 0; }

class LocalTlsConnection : public HttpConnection {
    int fd;
    tls_stream *stream;
    unsigned plain_bytes;
    bool http_failed;
    static bool wait_fd(int fd,short events,uint32_t timeout) {
        pollfd p={fd,events,0};return poll(&p,1,(int)timeout)>0;
    }
    static int tcp_open(void *context,const char *,uint16_t port,uint32_t timeout) {
        LocalTlsConnection *self=(LocalTlsConnection *)context;
        int fd=self->fd=socket(AF_INET,SOCK_STREAM,0);
        if(fd<0 || fcntl(fd,F_SETFL,O_NONBLOCK))return -1;
        sockaddr_in address={};address.sin_family=AF_INET;
        address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        int result=connect(fd,(sockaddr *)&address,sizeof address);
        if(result && errno==EINPROGRESS && wait_fd(fd,POLLOUT,timeout)) {
            int error=0;socklen_t size=sizeof error;
            if(!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size) && !error)return 0;
        }
        return result;
    }
    static int tcp_read(void *context,uint8_t *data,size_t n,uint32_t timeout) {
        int fd=((LocalTlsConnection *)context)->fd;
        assert(n<=512 && timeout);
        return wait_fd(fd,POLLIN,timeout)?(int)recv(fd,data,n>17?17:n,0):-1;
    }
    static int tcp_write(void *context,const uint8_t *data,size_t n,uint32_t timeout) {
        int fd=((LocalTlsConnection *)context)->fd;
        assert(n<=512 && timeout);
        return wait_fd(fd,POLLOUT,timeout)?(int)send(fd,data,n>19?19:n,MSG_NOSIGNAL):-1;
    }
    static void tcp_close(void *context) {
        LocalTlsConnection *self=(LocalTlsConnection *)context;
        if(self->fd>=0)close(self->fd);
        self->fd=-1;
    }
public:
    LocalTlsConnection():fd(-1),plain_bytes(0),http_failed(false) {
        tls_stream_port port={this,monotonic_ms,clock_ready,NULL,trust_roots,tcp_open,tcp_read,tcp_write,tcp_close};
        stream=tls_stream_create(&port);assert(stream);created++;
    }
    ~LocalTlsConnection() {
        if(http_failed) {
            failed_diagnostic=tls_stream_diagnostic(stream);
            failed_plain_bytes=plain_bytes;failed_connections++;
        }
        tls_stream_destroy(stream);assert(fd==-1);destroyed++;
    }
    void failure(const char *stage,int code,int) override {
        if(http_failed)return;
        http_failed=true;failed_http_code=code;
        snprintf(failed_http_stage,sizeof failed_http_stage,"%s",stage);
    }
    int open(const char *host,uint16_t port) override {
        return tls_stream_open(stream,host,port,2000)==HTTPS_OK?0:-1;
    }
    int read(void *p,int n) override {
        int result=tls_stream_read(stream,(uint8_t *)p,n>512?512:n);
        if(result>0)plain_bytes+=(unsigned)result;
        return result;
    }
    int write(const void *p,int n) override { return tls_stream_write(stream,(const uint8_t *)p,n>512?512:n); }
};
static HttpConnection *local_factory() { return new LocalTlsConnection(); }
static void matches(Message *message,const void *data,size_t size)
{
    if(message->length!=(int)size || memcmp(message->message,data,size)) {
        fprintf(stderr,"Expected %zu bytes [%.*s], received %d [%.*s]\n",size,(int)size,(const char *)data,
                message->length,message->length,message->message);
        abort();
    }
}

int test_real_tls_uci(HttpTarget *target,int argc,char **argv)
{
    assert(argc==6 && psa_crypto_init()==PSA_SUCCESS);
    mbedtls_x509_crt_init(&roots);assert(!mbedtls_x509_crt_parse_file(&roots,argv[2]));
    const char *scenario=argv[4];
    Message *reply,*status;
    uint8_t create[160]={6,HTTP_CMD_HEADER_CREATE,1};
    snprintf((char *)create+3,sizeof create-3,"https://%s:%u/data",argv[5],(unsigned)atoi(argv[3]));
    Message command={(int)strlen((char *)create+3)+4,true,create};
    http_set_secure_connection_factory(local_factory);
    target->parse_command(&command,&reply,&status);
    matches(status,"000 OK",6);assert(reply->length==1);
    bool object=!strcmp(scenario,"object") || !strcmp(scenario,"long-status");
    uint8_t exchange[]={6,(uint8_t)(object?HTTP_CMD_DO_EXCHANGE_OBJ:HTTP_CMD_DO_EXCHANGE_RAW),reply->message[0],255};
    Message request={sizeof exchange,true,exchange};
    target->parse_command(&request,&reply,&status);
    bool body_fault=!strncmp(scenario,"body-",5);
    if(!strcmp(scenario,"failure") || body_fault) {
        matches(status,"503 SERVICE UNAVAILABLE",23);assert(!reply->length);
        if(body_fault) {
            // The real TLS/parser path consumed a partial body, but UCI exposed none.
            assert(created==1 && destroyed==1 && failed_connections==1);
            assert(failed_plain_bytes>=1024 && !strcmp(failed_http_stage,"http_read"));
            assert(failed_diagnostic.stage==TLS_STAGE_READ && failed_diagnostic.verify_flags==0);
            if(!strcmp(scenario,"body-clean")) {
                assert(failed_diagnostic.status==HTTPS_OK && failed_http_code==0);
            } else if(!strcmp(scenario,"body-eof")) {
                assert(failed_diagnostic.status==HTTPS_TRUNCATED && failed_diagnostic.code==0);
                assert(failed_http_code==-1);
            } else if(!strcmp(scenario,"body-reset")) {
                assert(failed_diagnostic.status==HTTPS_TRANSPORT_ERROR);
                assert(failed_diagnostic.code==MBEDTLS_ERR_SSL_INTERNAL_ERROR && failed_http_code==-1);
            } else {
                assert(!strcmp(scenario,"body-stall") && failed_diagnostic.status==HTTPS_TIMEOUT);
                assert(failed_http_code==-1);
            }
            printf("Captured authenticated body fault: %s bytes=%u stage=%u status=%u code=%d verify=%u http=%s/%d\n",
                   scenario,failed_plain_bytes,(unsigned)failed_diagnostic.stage,(unsigned)failed_diagnostic.status,
                   failed_diagnostic.code,failed_diagnostic.verify_flags,failed_http_stage,failed_http_code);
            tls_stream_diagnostics captured=failed_diagnostic;
            uint8_t release[]={6,HTTP_CMD_HEADER_FREE,exchange[2]};
            Message free_header={sizeof release,true,release};
            target->parse_command(&free_header,&reply,&status);matches(status,"000 OK",6);
            target->parse_command(&command,&reply,&status);matches(status,"000 OK",6);
            assert(reply->length==1 && reply->message[0]==0);
            exchange[2]=reply->message[0];
            target->parse_command(&request,&reply,&status);
            assert(status->length>=17 && !memcmp(status->message,"HTTP/1.1 200 OK\r\n",17));
            matches(reply,"{\"x\":1}",7);
            assert(created==2 && destroyed==2 && failed_connections==1);
            assert(!memcmp(&captured,&failed_diagnostic,sizeof captured));
            target->parse_command(&free_header,&reply,&status);matches(status,"000 OK",6);
        }
    } else {
        if(object) {
            if(!strcmp(scenario,"long-status")) {
                // The status-length hardware register is only eight bits.
                assert(status->length==255);
                assert(!memcmp(status->message,"200 ",4));
                for(int i=4;i<status->length;i++)assert(status->message[i]=='x');
            } else matches(status,"200 OK",6);
            assert(reply->length==2);
            uint8_t query[]={6,HTTP_CMD_BODY_QUERY,reply->message[1],'x',0};
            Message q={sizeof query,true,query};target->parse_command(&q,&reply,&status);
            const uint8_t wanted[]={HTTP_DATA_INTEGER,1,0,0,0};matches(reply,wanted,sizeof wanted);
        } else {
            const char *expected_status=!strcmp(scenario,"http-error")?"HTTP/1.1 404 OK\r\n":"HTTP/1.1 200 OK\r\n";
            assert(status->length>=17 && !memcmp(status->message,expected_status,17));
            assert(status->length<=255);
            if(!strcmp(scenario,"raw-long-header"))assert(status->length==255);
            if(!strncmp(scenario,"long-",5)) {
                size_t expected=(size_t)atoi(scenario+5);
                size_t total=0;unsigned parts=0;
                for(;;) {
                    // A full 896-byte FPGA response queue never deasserts
                    // DATA_AV. Enforce readable blocks, not just total bytes.
                    assert(reply->length<=895);
                    for(int i=0;i<reply->length;i++)assert(reply->message[i]=='x');
                    total+=reply->length;parts++;assert(parts<=4);
                    if(reply->last_part)break;
                    target->get_more_data(&reply,&status);
                }
                assert(total==expected && parts==expected/895+1);
            } else if(!strcmp(scenario,"binary")) {
                const uint8_t wanted[]={'a',0,'b'};matches(reply,wanted,sizeof wanted);
            } else matches(reply,"{\"x\":1}",7);
        }
    }
    target->c64_reset();http_set_secure_connection_factory(NULL);
    assert(created==(body_fault?2u:1u) && destroyed==created);
    mbedtls_x509_crt_free(&roots);
    printf("UCI through real TLS: %s passed; connection cleaned up.\n",scenario);
    return 0;
}
