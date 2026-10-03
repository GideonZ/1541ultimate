#include "tls_uart.h"
#include "tls_service.h"
#include "tls_stream.h"
#include "tls_metrics.h"
#include "my_uart.h"
#include "rpc_calls.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <sys/time.h>
#if defined(MBEDTLS_PSA_CRYPTO_C)
#include "psa/crypto.h"
#endif

#define FRAME_SIZE (TLS_WIRE_HEADER + TLS_WIRE_CHUNK)
typedef struct { uint8_t bytes[FRAME_SIZE]; size_t length; int64_t queued; } frame;
static QueueHandle_t requests, replies;
static tls_stream *stream;
static uint32_t session, sequence;
static uint64_t epoch;
static int64_t deadline;
static tls_service service;
static uint32_t failure_count, failure_session;
static uint64_t failure_epoch;
static tls_stream_diagnostics failure;

/* Latched before teardown; successful requests and CLOSE cannot erase it. */
static void record_failure(void)
{
    if (failure_count<INT32_MAX) ++failure_count;
    failure_session=session; failure_epoch=epoch;
    failure=tls_stream_diagnostic(stream);
    if (!stream) failure.status=HTTPS_TRANSPORT_ERROR;
}

/* Called only by the TLS worker. At most one nonblocking packet per second;
 * a full UART queue drops telemetry instead of delaying application traffic. */
static void publish_metrics(void)
{
    static int64_t last = -1000;
    static uint32_t boot_id, sequence;
    int64_t now=esp_timer_get_time()/1000;
    if (now-last<1000) return;
    last=now;
    if (!boot_id) { boot_id=esp_random(); if (!boot_id) boot_id=1; }
    command_buf_t *b;
    if (my_uart_get_buffer(UART_NUM_1,&b,0)!=pdTRUE) return;
    tls_metrics sample;
    unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    sample.value[TLS_METRIC_INTERNAL_FREE]=heap_caps_get_free_size(caps);
    sample.value[TLS_METRIC_INTERNAL_MIN]=heap_caps_get_minimum_free_size(caps);
    sample.value[TLS_METRIC_INTERNAL_LARGEST]=heap_caps_get_largest_free_block(caps);
    sample.value[TLS_METRIC_8BIT_FREE]=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    sample.value[TLS_METRIC_8BIT_MIN]=heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    sample.value[TLS_METRIC_8BIT_LARGEST]=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    /* IDF Xtensa StackType_t is uint8_t; multiplication also makes the byte
     * unit explicit if this code is compiled for a different FreeRTOS port. */
    sample.value[TLS_METRIC_STACK_MIN_BYTES]=uxTaskGetStackHighWaterMark(NULL)*sizeof(StackType_t);
    sample.value[TLS_METRIC_UPTIME_SECONDS]=(uint32_t)(now/1000);
    sample.value[TLS_METRIC_BOOT_ID]=boot_id;
    sample.value[TLS_METRIC_SEQUENCE]=++sequence;
    sample.value[TLS_METRIC_FAILURE_COUNT]=failure_count;
    sample.value[TLS_METRIC_FAILURE_EPOCH_LO]=(uint32_t)failure_epoch;
    sample.value[TLS_METRIC_FAILURE_EPOCH_HI]=(uint32_t)(failure_epoch>>32);
    sample.value[TLS_METRIC_FAILURE_SESSION]=failure_session;
    sample.value[TLS_METRIC_FAILURE_STAGE]=failure.stage;
    sample.value[TLS_METRIC_FAILURE_STATUS]=failure.status;
    sample.value[TLS_METRIC_FAILURE_CODE]=(uint32_t)failure.code;
    sample.value[TLS_METRIC_FAILURE_VERIFY_FLAGS]=failure.verify_flags;
    b->data[0]=CMD_TLS_METRICS; b->data[1]=0xfe; b->data[2]=b->data[3]=0;
    tls_metrics_encode(b->data+4,&sample); b->size=4+TLS_METRICS_SIZE;
    (void)my_uart_try_transmit_packet(UART_NUM_1,b);
}

static int64_t now_ms(void *unused) { (void)unused; return esp_timer_get_time()/1000; }
static uint32_t remaining(void)
{
    int64_t left=deadline-now_ms(NULL);
    return left>0 ? (uint32_t)left : 0;
}
static bool transmit(const uint8_t *p,size_t n)
{
    command_buf_t *b;
    if(my_uart_get_buffer(UART_NUM_1,&b,0)!=pdTRUE) return false;
    b->data[0]=CMD_TLS_STREAM;b->data[1]=0xfe;b->data[2]=b->data[3]=0;
    memcpy(b->data+4,p,n);b->size=(int)n+4;
    return my_uart_try_transmit_packet(UART_NUM_1,b)==pdTRUE;
}
static int tunnel(unsigned op,const uint8_t *input,size_t count,uint8_t *output,uint32_t timeout)
{
    if(!timeout || count>TLS_WIRE_CHUNK || sequence==UINT32_MAX) return -1;
    frame response;
    while(xQueueReceive(replies,&response,0)==pdTRUE) {}
    uint8_t request[FRAME_SIZE]={ 'U','S',TLS_WIRE_VERSION,TLS_WIRE_REQUEST,TLS_WIRE_TCP,(uint8_t)op };
    tls_wire_put_epoch(request,epoch);
    tls_wire_put32(request+8,session);tls_wire_put32(request+12,++sequence);
    tls_wire_put16(request+16,(uint16_t)timeout);tls_wire_put16(request+18,(uint16_t)count);
    size_t size=TLS_WIRE_HEADER;
    if(input) {memcpy(request+size,input,count);size+=count;}
    int64_t until=now_ms(NULL)+timeout;
    if(!transmit(request,size)) return -1;
    if(op==TLS_WIRE_CLOSE) return 0;
    for(;;) {
        int64_t left=until-now_ms(NULL);
        if(left<=0 || xQueueReceive(replies,&response,pdMS_TO_TICKS(left)+1)!=pdTRUE) return -1;
        if(!tls_wire_matches(request,size,response.bytes,response.length)) continue;
        if(now_ms(NULL)>=until) return -1;
        int32_t result=(int32_t)tls_wire_get32(response.bytes+20);
        if(result<0) return -1;
        size_t bytes=tls_wire_get16(response.bytes+18);
        if(bytes && output) memcpy(output,response.bytes+TLS_WIRE_HEADER,bytes);
        return result;
    }
}
static bool sync_time(void *unused)
{
    (void)unused;uint8_t reply[8];int64_t start=now_ms(NULL);
    if(tunnel(TLS_WIRE_TIME,NULL,0,reply,remaining())) return false;
    uint32_t utc=tls_wire_get32(reply),age=tls_wire_get32(reply+4);
    uint32_t transit=(uint32_t)(now_ms(NULL)-start)/1000+1;
    if(utc<1704067200u || utc>=4102444800u || age>86400u-transit) return false;
    struct timeval tv={ .tv_sec=(time_t)(utc+transit), .tv_usec=0 };
    return settimeofday(&tv,NULL)==0;
}
static int trust(void *unused,mbedtls_ssl_config *config)
{ (void)unused;return esp_crt_bundle_attach(config); }
static int tcp_open(void *unused,const char *host,uint16_t port,uint32_t timeout)
{
    (void)unused;uint8_t bytes[HTTPS_MAX_HOST+2];size_t n=strlen(host);
    tls_wire_put16(bytes,port);memcpy(bytes+2,host,n);
    return tunnel(TLS_WIRE_OPEN,bytes,n+2,NULL,timeout);
}
static int tcp_read(void *unused,uint8_t *p,size_t n,uint32_t ms)
{ (void)unused;return tunnel(TLS_WIRE_READ,NULL,n,p,ms); }
static int tcp_write(void *unused,const uint8_t *p,size_t n,uint32_t ms)
{ (void)unused;return tunnel(TLS_WIRE_WRITE,p,n,NULL,ms); }
static void tcp_close(void *unused)
{ (void)unused;(void)tunnel(TLS_WIRE_CLOSE,NULL,0,NULL,1); }
static int open_tls(void *unused,uint32_t job,uint64_t generation,const char *host,uint16_t port,uint32_t ms)
{
    (void)unused;session=job;epoch=generation;sequence=0;deadline=now_ms(NULL)+ms;
    tls_stream_port transport={NULL,now_ms,sync_time,NULL,trust,tcp_open,tcp_read,tcp_write,tcp_close};
    stream=tls_stream_create(&transport);
    if(!stream) {record_failure();return -HTTPS_TRANSPORT_ERROR;}
    https_status status=tls_stream_open(stream,host,port,ms);
    if(status!=HTTPS_OK) {record_failure();tls_stream_destroy(stream);stream=NULL;return -(int)status;}
    return 0;
}
static int read_tls(void *unused,uint8_t *bytes,size_t n)
{
    (void)unused;int result=tls_stream_read(stream,bytes,n);
    if(result<0) record_failure();
    return result<0 ? -(int)tls_stream_status(stream) : result;
}
static int write_tls(void *unused,const uint8_t *bytes,size_t n)
{
    (void)unused;int result=tls_stream_write(stream,bytes,n);
    if(result<0) record_failure();
    return result<0 ? -(int)tls_stream_status(stream) : result;
}
static void close_tls(void *unused)
{ (void)unused;tls_stream_destroy(stream);stream=NULL; }
static void worker(void *unused)
{
    (void)unused;frame request;
    for(;;) {
        if(xQueueReceive(requests,&request,pdMS_TO_TICKS(100))==pdTRUE) {
            size_t n=tls_service_handle(&service,request.bytes,request.length,request.bytes,request.queued,now_ms(NULL));
            if(n && !transmit(request.bytes,n)) tls_service_expire(&service,INT64_MAX);
        }
        tls_service_expire(&service,now_ms(NULL));
        publish_metrics();
    }
}
void tls_uart_start(void)
{
    if(requests) return;
#if defined(MBEDTLS_PSA_CRYPTO_C)
    if(psa_crypto_init()!=PSA_SUCCESS) return;
#endif
    requests=xQueueCreate(1,sizeof(frame));replies=xQueueCreate(2,sizeof(frame));
    tls_service_port port={NULL,open_tls,read_tls,write_tls,close_tls};
    tls_service_init(&service,&port,esp_random());
    if(!requests || !replies || xTaskCreate(worker,"TLS stream",12288,NULL,tskIDLE_PRIORITY+1,NULL)!=pdPASS) {
        if(requests)vQueueDelete(requests);
        if(replies)vQueueDelete(replies);
        requests=replies=NULL;
    }
}
void tls_uart_receive(command_buf_t *b)
{
    if(b->size>=4+TLS_WIRE_HEADER && b->size<=4+FRAME_SIZE && b->data[1]==0xfe &&
       tls_wire_valid(b->data+4,(size_t)b->size-4)) {
        frame f;f.length=(size_t)b->size-4;f.queued=now_ms(NULL);memcpy(f.bytes,b->data+4,f.length);
        QueueHandle_t queue=NULL;
        if(f.bytes[3]==TLS_WIRE_REQUEST && f.bytes[4]==TLS_WIRE_TLS) queue=requests;
        if(f.bytes[3]==TLS_WIRE_REPLY && f.bytes[4]==TLS_WIRE_TCP) queue=replies;
        if(queue) (void)xQueueSend(queue,&f,0);
    }
    my_uart_free_buffer(UART_NUM_1,b);
}
