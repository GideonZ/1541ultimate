#include "https_management.h"
#include "wifi_cmd.h"
#include "http_connection.h"
#include "tls_wire.h"
#include "tls_epoch.h"
#include "https_client.h"
#include "tls_metrics.h"
#include "https_failure.h"
#include "json.h"
#include "filemanager.h"
#include "socket.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include <errno.h>
#include <string.h>

struct Frame { uint8_t bytes[TLS_WIRE_HEADER+TLS_WIRE_CHUNK]; size_t size; TickType_t queued; };
static QueueHandle_t replies, requests;
static SemaphoreHandle_t client_lock;
static volatile uint32_t active_session;
static uint64_t active_epoch;
static volatile bool controller_ready;
static uint32_t sync_utc;
static TickType_t sync_tick;
static int tcp_fd=-1;
static uint32_t tcp_owner,tcp_sequence;
static uint64_t tcp_epoch;
static TickType_t tcp_start;
static uint32_t tcp_timeout;
static tls_metrics metrics_cache;
static TickType_t metrics_tick;
static bool metrics_valid;
static volatile bool metrics_supported;
static uint16_t detected_major, detected_minor;
static uint32_t metrics_received, metrics_accepted;
static bool worker_ready;
static const char *connection_state="not_requested";
static https_failure last_failure={};
/* Keep the first, most local failure for an epoch. Wrapper errors and cleanup
 * cannot overwrite DNS/socket evidence; later successful epochs keep it. */
static void record_failure(uint64_t epoch,uint32_t session,const char *stage,int code,int detail=0)
{
    taskENTER_CRITICAL();
    https_failure_record(&last_failure,epoch,session,stage,code,detail);
    taskEXIT_CRITICAL();
}

static uint32_t elapsed(TickType_t start)
{ return (uint32_t)(xTaskGetTickCount()-start)*portTICK_PERIOD_MS; }
static bool is_active(uint32_t job,uint64_t epoch)
{
    taskENTER_CRITICAL();
    bool matches=job==active_session && epoch==active_epoch;
    taskEXIT_CRITICAL();
    return matches;
}

/* Private monotonic state, outside exported configuration. Never truncate an
 * existing slot: keeping its allocation reduces metadata changes at boot. */
static const char *epoch_files[2]={".https-epoch-a",".https-epoch-b"};
static int load_epoch(void *,unsigned slot,uint8_t *record)
{
    FileManager *fm=FileManager::getFileManager();File *file=NULL;
    FRESULT result=fm->fopen("/flash",epoch_files[slot],FA_READ,&file);
    if(result==FR_NO_FILE)return 0;
    if(result!=FR_OK)return -1;
    uint32_t read=0;
    bool valid=file->get_size()==TLS_EPOCH_RECORD_SIZE &&
        file->read(record,TLS_EPOCH_RECORD_SIZE,&read)==FR_OK && read==TLS_EPOCH_RECORD_SIZE;
    fm->fclose(file);
    return valid?1:-1;
}
static bool save_epoch(void *,unsigned slot,const uint8_t *record)
{
    FileManager *fm=FileManager::getFileManager();File *file=NULL;
    if(fm->fopen("/flash",epoch_files[slot],FA_WRITE|FA_OPEN_ALWAYS,&file)!=FR_OK)return false;
    uint32_t written=0;
    bool valid=file->write(record,TLS_EPOCH_RECORD_SIZE,&written)==FR_OK &&
        written==TLS_EPOCH_RECORD_SIZE && file->sync()==FR_OK;
    fm->fclose(file);
    return valid;
}
static bool next_epoch(uint64_t *epoch)
{
    static uint32_t boot,counter;
    static bool attempted;
    if(!attempted) {
        attempted=true;
        tls_epoch_store store={NULL,load_epoch,save_epoch};
        if(!tls_epoch_advance(&store,&boot))return false;
    }
    return tls_epoch_next(boot,&counter,epoch);
}
static bool send_frame(const uint8_t *bytes,size_t size)
{
    command_buf_t *b;
    if(!esp32.uart || esp32.uart->GetBuffer(&b,0)!=pdTRUE) return false;
    b->data[0]=CMD_TLS_STREAM;b->data[1]=0xfe;b->data[2]=b->data[3]=0;
    memcpy(b->data+4,bytes,size);b->size=(int)size+4;
    return esp32.uart->TryTransmitPacket(b)==pdTRUE;
}
bool https_management_rx(command_buf_context_t *context,command_buf_t *b,BaseType_t *woken)
{
    if(b->size>=4 && b->data[0]==CMD_TLS_METRICS && b->data[1]==0xfe) {
        if(metrics_received<INT32_MAX) ++metrics_received;
        if(metrics_supported && b->data[2]==0 && b->data[3]==0 &&
           tls_metrics_decode(&metrics_cache,b->data+4,(size_t)b->size-4)) {
            metrics_tick=xTaskGetTickCountFromISR(); metrics_valid=true;
            if(metrics_accepted<INT32_MAX) ++metrics_accepted;
        }
        cmd_buffer_free_isr(context,b,woken);
        return true;
    }
    if(b->size<4 || b->data[0]!=CMD_TLS_STREAM || b->data[1]!=0xfe) return false;
    if(b->size>=4+TLS_WIRE_HEADER && b->size<=4+(int)sizeof(Frame::bytes)) {
        /* This receive ISR is the sole writer; queues copy before reuse. */
        static Frame f;f.size=(size_t)b->size-4;f.queued=xTaskGetTickCountFromISR();
        memcpy(f.bytes,b->data+4,f.size);
        QueueHandle_t queue=NULL;
        if(f.bytes[3]==TLS_WIRE_REPLY && f.bytes[4]==TLS_WIRE_TLS) queue=replies;
        if(f.bytes[3]==TLS_WIRE_REQUEST && f.bytes[4]==TLS_WIRE_TCP) queue=requests;
        if(queue) (void)xQueueSendFromISR(queue,&f,woken);
    }
    cmd_buffer_free_isr(context,b,woken);
    return true;
}
void https_management_time(uint32_t seconds)
{
    taskENTER_CRITICAL();
    sync_utc=seconds>=1704067200u && seconds<4102444800u ? seconds : 0;
    sync_tick=xTaskGetTickCount();
    taskEXIT_CRITICAL();
}
void https_management_version(uint16_t major,uint16_t minor)
{
    taskENTER_CRITICAL();
    detected_major=major; detected_minor=minor;
    controller_ready=major==1 && minor>=16;
    metrics_supported=major==1 && minor>=17;
    metrics_valid=false;
    taskEXIT_CRITICAL();
}

static void metric_identifier(char *text,uint32_t value)
{
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<8;++i) text[i]=digits[(value>>(28-4*i))&15];
    text[8]=0;
}

void https_management_add_metrics(JSON_Object *json)
{
    tls_metrics sample; uint32_t age,received,accepted; https_failure failure;
    uint16_t major,minor; bool ready,initialized,clock_valid; const char *state;
    taskENTER_CRITICAL();
    age=elapsed(metrics_tick);
    if(age>TLS_METRICS_MAX_AGE_MS) metrics_valid=false;
    bool valid=metrics_supported && metrics_valid;
    if(valid) sample=metrics_cache;
    major=detected_major; minor=detected_minor; ready=controller_ready;
    initialized=worker_ready; received=metrics_received; accepted=metrics_accepted;
    clock_valid=sync_utc!=0 && elapsed(sync_tick)<=86400000u;
    state=connection_state;
    failure=last_failure;
    taskEXIT_CRITICAL();
    JSON_Object *object=JSON::Obj(); json->add("esp32",object);
    object->add("available",valid);
    JSON_Object *bridge=JSON::Obj(); json->add("https_bridge",bridge);
    bridge->add("controller_major",(int)major); bridge->add("controller_minor",(int)minor);
    bridge->add("controller_compatible",ready); bridge->add("socket_worker_ready",initialized);
    bridge->add("clock_synchronized",clock_valid); bridge->add("last_connection_state",state);
    bridge->add("metrics_received",(int)received); bridge->add("metrics_accepted",(int)accepted);
    JSON_Object *local=JSON::Obj(); bridge->add("last_failure",local);
    local->add("count",(int)failure.count);
    if(failure.count) {
        char epoch_text[17],session_text[9];
        metric_identifier(epoch_text,(uint32_t)(failure.epoch>>32));
        metric_identifier(epoch_text+8,(uint32_t)failure.epoch);
        metric_identifier(session_text,failure.session);
        local->add("epoch",epoch_text); local->add("session",session_text);
        local->add("stage",failure.stage); local->add("code",failure.code);
        local->add("detail",failure.detail);
    }
    if(!valid) return; // Missing/stale observations are never reported as zero.
    object->add("sample_age_ms",(int)age);
    static const char *keys[]={"internal_free", "internal_min_ever_free", "internal_largest_free_block",
        "free_8bit", "min_ever_free_8bit", "largest_free_block_8bit", "tls_stack_min_free_bytes", "uptime_seconds"};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i) object->add(keys[i],(int)sample.value[i]);
    char identifier[9];
    metric_identifier(identifier,sample.value[TLS_METRIC_BOOT_ID]);
    object->add("boot_id",identifier);
    metric_identifier(identifier,sample.value[TLS_METRIC_SEQUENCE]);
    object->add("sample_sequence",identifier);
    if(sample.version>=2) {
        JSON_Object *fault=JSON::Obj(); object->add("last_tls_failure",fault);
        fault->add("count",(int)sample.value[TLS_METRIC_FAILURE_COUNT]);
        if(sample.value[TLS_METRIC_FAILURE_COUNT]) {
            char epoch_text[17];
            metric_identifier(epoch_text,sample.value[TLS_METRIC_FAILURE_EPOCH_HI]);
            metric_identifier(epoch_text+8,sample.value[TLS_METRIC_FAILURE_EPOCH_LO]);
            fault->add("epoch",epoch_text);
            metric_identifier(identifier,sample.value[TLS_METRIC_FAILURE_SESSION]);
            fault->add("session",identifier);
            fault->add("stage",(int)sample.value[TLS_METRIC_FAILURE_STAGE]);
            fault->add("status",(int)sample.value[TLS_METRIC_FAILURE_STATUS]);
            fault->add("code",(int)(int32_t)sample.value[TLS_METRIC_FAILURE_CODE]);
            metric_identifier(identifier,sample.value[TLS_METRIC_FAILURE_VERIFY_FLAGS]);
            fault->add("verify_flags",identifier);
        }
    }
}

/* DNS work is scheduled on lwIP's thread; no stack pointer outlives a timeout.
 * Late callbacks have a generation tag and cannot overwrite a newer lookup. */
static uint32_t dns_generation;
static char dns_name[HTTPS_MAX_HOST+1];
static bool dns_done,dns_ok;
static ip_addr_t dns_address;
static void dns_found(const char *,const ip_addr_t *address,void *tag)
{
    taskENTER_CRITICAL();
    if((uint32_t)(uintptr_t)tag==dns_generation) {
        dns_done=true;dns_ok=address!=NULL;
        if(address) dns_address=*address;
    }
    taskEXIT_CRITICAL();
}
static void dns_begin(void *tag)
{
    char name[HTTPS_MAX_HOST+1];bool valid;
    taskENTER_CRITICAL();
    valid=(uint32_t)(uintptr_t)tag==dns_generation;
    if(valid) memcpy(name,dns_name,sizeof name);
    taskEXIT_CRITICAL();
    if(!valid)return;
    ip_addr_t address;
    err_t result=dns_gethostbyname(name,&address,dns_found,tag);
    if(result==ERR_OK)dns_found(name,&address,tag);
    else if(result!=ERR_INPROGRESS)dns_found(name,NULL,tag);
}
static bool resolve(const char *host,ip_addr_t *address,TickType_t start,uint32_t timeout,uint32_t job,uint64_t epoch)
{
    if(dns_generation==UINT32_MAX){record_failure(epoch,job,"dns_generation",0);return false;}
    taskENTER_CRITICAL();
    ++dns_generation;dns_done=dns_ok=false;strcpy(dns_name,host);
    uint32_t tag=dns_generation;
    taskEXIT_CRITICAL();
    err_t queued=tcpip_try_callback(dns_begin,(void *)(uintptr_t)tag);
    if(queued!=ERR_OK){record_failure(epoch,job,"dns_queue",queued);return false;}
    while(elapsed(start)<timeout) {
        taskENTER_CRITICAL();
        bool done=dns_done,ok=dns_ok;if(done && ok)*address=dns_address;
        taskEXIT_CRITICAL();
        if(done){if(!ok)record_failure(epoch,job,"dns_lookup",0);return ok;}
        vTaskDelay(1);
    }
    record_failure(epoch,job,"dns_timeout",0);return false;
}
static void close_tcp()
{ if(tcp_fd>=0)lwip_close(tcp_fd);tcp_fd=-1;tcp_owner=0; }
static bool wait_tcp(bool write,TickType_t start,uint32_t timeout)
{
    uint32_t used=elapsed(start);if(used>=timeout){if(tcp_owner)record_failure(tcp_epoch,tcp_owner,"tcp_deadline",0,write?1:0);return false;}
    uint32_t ms=timeout-used;fd_set set;FD_ZERO(&set);FD_SET(tcp_fd,&set);
    timeval tv={(long)(ms/1000),(long)(ms%1000)*1000};
    int result=lwip_select(tcp_fd+1,write?NULL:&set,write?&set:NULL,NULL,&tv);
    if(result<=0 && tcp_owner)record_failure(tcp_epoch,tcp_owner,result==0?"tcp_timeout":"tcp_select",result<0?errno:0,write?1:0);
    return result>0;
}
static int open_tcp(const char *host,uint16_t port,uint32_t job,uint64_t epoch,TickType_t start,uint32_t timeout)
{
    ip_addr_t address;
    if(!resolve(host,&address,start,timeout,job,epoch) || elapsed(start)>=timeout || !is_active(job,epoch))return -1;
    tcp_fd=lwip_socket(AF_INET,SOCK_STREAM,0);if(tcp_fd<0){record_failure(epoch,job,"tcp_socket",errno);return -1;}
    unsigned long nonblocking=1;
    if(lwip_ioctl(tcp_fd,FIONBIO,&nonblocking)) {record_failure(epoch,job,"tcp_nonblocking",errno);close_tcp();return -1;}
    sockaddr_in remote={};remote.sin_family=AF_INET;remote.sin_port=htons(port);
    remote.sin_addr.s_addr=ip4_addr_get_u32(ip_2_ip4(&address));
    int result=lwip_connect(tcp_fd,(sockaddr *)&remote,sizeof remote);
    int connect_error=result?errno:0;
    if(result && (errno==EINPROGRESS || errno==EWOULDBLOCK) && wait_tcp(true,start,timeout)) {
        int error=0;socklen_t n=sizeof error;
        if(lwip_getsockopt(tcp_fd,SOL_SOCKET,SO_ERROR,&error,&n))connect_error=errno;
        else {connect_error=error;if(!error)result=0;}
    }
    if(result || elapsed(start)>=timeout || !is_active(job,epoch)) {
        record_failure(epoch,job,elapsed(start)>=timeout?"tcp_connect_timeout":"tcp_connect",connect_error);
        close_tcp();return -1;
    }
    tcp_owner=job;tcp_epoch=epoch;tcp_start=start;tcp_timeout=timeout;return 0;
}
static void expire_tcp_owner()
{
    if(!tcp_owner)return;
    if(!is_active(tcp_owner,tcp_epoch)) {close_tcp();return;}
    if(elapsed(tcp_start)>=tcp_timeout) {
        record_failure(tcp_epoch,tcp_owner,"tcp_session_deadline",0);
        close_tcp();
    }
}
static bool tcp_request_live(uint32_t job,uint64_t epoch,TickType_t queued,uint32_t timeout,unsigned op)
{
    if(!is_active(job,epoch))return false;
    if(elapsed(queued)<timeout)return true;
    record_failure(epoch,job,"tcp_queue_deadline",0,(int)op);
    return false;
}
static void socket_worker(void *)
{
    Frame f;
    for(;;) {
        expire_tcp_owner();
        taskENTER_CRITICAL();
        if(sync_utc && elapsed(sync_tick)>86400000u)sync_utc=0;
        taskEXIT_CRITICAL();
        if(xQueueReceive(requests,&f,pdMS_TO_TICKS(50)+1)!=pdTRUE)continue;
        uint8_t *p=f.bytes;
        if(!tls_wire_valid(p,f.size) || p[3]!=TLS_WIRE_REQUEST || p[4]!=TLS_WIRE_TCP)continue;
        uint32_t job=tls_wire_get32(p+8),seq=tls_wire_get32(p+12);
        uint64_t epoch=tls_wire_get_epoch(p);
        unsigned op=p[5],count=tls_wire_get16(p+18),timeout=tls_wire_get16(p+16);
        int result=-HTTPS_TRANSPORT_ERROR;size_t payload=0;
        if(op==TLS_WIRE_CLOSE && job==tcp_owner && epoch==tcp_epoch) {close_tcp();result=0;}
        else if(tcp_request_live(job,epoch,f.queued,timeout,op)) {
            if(op==TLS_WIRE_TIME) {
                taskENTER_CRITICAL();
                uint32_t utc=sync_utc,age=elapsed(sync_tick)/1000;
                taskEXIT_CRITICAL();
                if(utc && age<=86400) {tls_wire_put32(p+TLS_WIRE_HEADER,utc+age);tls_wire_put32(p+TLS_WIRE_HEADER+4,age);payload=8;result=0;}
            } else if(op==TLS_WIRE_OPEN && !tcp_owner) {
                char host[HTTPS_MAX_HOST+1];memcpy(host,p+TLS_WIRE_HEADER+2,count-2);host[count-2]=0;
                result=open_tcp(host,tls_wire_get16(p+TLS_WIRE_HEADER),job,epoch,f.queued,timeout);
                if(!result)tcp_sequence=seq;
            } else if(job==tcp_owner && epoch==tcp_epoch && seq>tcp_sequence) {
                tcp_sequence=seq;
                if(wait_tcp(op==TLS_WIRE_WRITE,f.queued,timeout)) {
                    if(op==TLS_WIRE_WRITE)result=lwip_send(tcp_fd,p+TLS_WIRE_HEADER,count,0);
                    else if(op==TLS_WIRE_READ) {result=lwip_recv(tcp_fd,p+TLS_WIRE_HEADER,count,0);if(result>0)payload=result;}
                    if(result<0)record_failure(epoch,job,op==TLS_WIRE_WRITE?"tcp_write":"tcp_read",errno);
                }
                if(result<0)close_tcp();
            }
        }
        p[3]=TLS_WIRE_REPLY;tls_wire_put16(p+16,0);tls_wire_put16(p+18,(uint16_t)payload);
        tls_wire_put32(p+20,(uint32_t)result);
        if(!send_frame(p,TLS_WIRE_HEADER+payload)) {
            if(op!=TLS_WIRE_CLOSE)record_failure(epoch,job,"tcp_reply_queue",0);
            if(job==tcp_owner && epoch==tcp_epoch)close_tcp();
        }
    }
}

class UartTlsConnection : public HttpConnection {
    uint32_t session,sequence;
    uint64_t epoch;
    TickType_t started;
    bool opened;
    int exchange(unsigned op,const uint8_t *data,size_t count,uint8_t *output) {
        uint32_t used=elapsed(started);
        if(sequence==UINT32_MAX || (used>=HTTPS_MAX_TIMEOUT_MS && op!=TLS_WIRE_CLOSE)) {
            if(op!=TLS_WIRE_CLOSE)record_failure(epoch,session,"uart_deadline",0,op);
            return -1;
        }
        Frame response;
        while(xQueueReceive(replies,&response,0)==pdTRUE) {}
        uint8_t request[TLS_WIRE_HEADER+TLS_WIRE_CHUNK]={'U','S',TLS_WIRE_VERSION,TLS_WIRE_REQUEST,TLS_WIRE_TLS,(uint8_t)op};
        tls_wire_put_epoch(request,epoch);
        tls_wire_put32(request+8,session);tls_wire_put32(request+12,++sequence);
        tls_wire_put16(request+16,(uint16_t)(op==TLS_WIRE_CLOSE?1:HTTPS_MAX_TIMEOUT_MS-used));
        tls_wire_put16(request+18,(uint16_t)count);
        size_t n=TLS_WIRE_HEADER;if(data){memcpy(request+n,data,count);n+=count;}
        if(!send_frame(request,n)){if(op!=TLS_WIRE_CLOSE)record_failure(epoch,session,"uart_send",0,op);return -1;}
        if(op==TLS_WIRE_CLOSE)return 0;
        for(;;) {
            used=elapsed(started);if(used>=HTTPS_MAX_TIMEOUT_MS){record_failure(epoch,session,"uart_deadline",0,op);return -1;}
            if(xQueueReceive(replies,&response,pdMS_TO_TICKS(HTTPS_MAX_TIMEOUT_MS-used)+1)!=pdTRUE){record_failure(epoch,session,"uart_reply_timeout",0,op);return -1;}
            if(!tls_wire_matches(request,n,response.bytes,response.size))continue;
            if(elapsed(started)>=HTTPS_MAX_TIMEOUT_MS){record_failure(epoch,session,"uart_deadline",0,op);return -1;}
            int32_t result=(int32_t)tls_wire_get32(response.bytes+20);
            if(result<0){record_failure(epoch,session,"tls_reply",result,op);return -1;}
            if(op==TLS_WIRE_READ && result)memcpy(output,response.bytes+TLS_WIRE_HEADER,(size_t)result);
            if(op==TLS_WIRE_RESERVE)memcpy(output,response.bytes+TLS_WIRE_HEADER,4);
            return result;
        }
    }
public:
    void failure(const char *stage,int code,int detail) override {record_failure(epoch,session,stage,code,detail);}
    UartTlsConnection(uint64_t generation):session(UINT32_MAX),sequence(0),epoch(generation),started(xTaskGetTickCount()),opened(false) {}
    ~UartTlsConnection() {
        if(opened)(void)exchange(TLS_WIRE_CLOSE,NULL,0,NULL);
        taskENTER_CRITICAL();active_session=0;active_epoch=0;taskEXIT_CRITICAL();
        xSemaphoreGive(client_lock);
    }
    int open(const char *host,uint16_t port) override {
        https_request r={};r.host=host;r.port=port;r.path="/";r.method="GET";r.timeout_ms=HTTPS_MAX_TIMEOUT_MS;
        if(opened || !https_request_valid(&r))return -1;
        uint8_t grant[4];
        if(exchange(TLS_WIRE_RESERVE,NULL,0,grant))return -1;
        session=tls_wire_get32(grant);
        taskENTER_CRITICAL();active_session=session;active_epoch=epoch;taskEXIT_CRITICAL();
        uint8_t bytes[HTTPS_MAX_HOST+2];size_t n=strlen(host);
        tls_wire_put16(bytes,port);memcpy(bytes+2,host,n);opened=true;
        return exchange(TLS_WIRE_OPEN,bytes,n+2,NULL);
    }
    int read(void *p,int n) override {return n>0?exchange(TLS_WIRE_READ,NULL,n>512?512:n,(uint8_t *)p):-1;}
    int write(const void *p,int n) override {return n>0?exchange(TLS_WIRE_WRITE,(const uint8_t *)p,n>512?512:n,NULL):-1;}
};
static HttpConnection *create_connection()
{
    if(!controller_ready){connection_state="controller_unavailable";return NULL;}
    if(!client_lock){connection_state="worker_unavailable";return NULL;}
    if(xSemaphoreTake(client_lock,0)!=pdTRUE){connection_state="busy";return NULL;}
    uint64_t epoch;
    if(!next_epoch(&epoch)){connection_state="epoch_storage_failed";xSemaphoreGive(client_lock);return NULL;}
    HttpConnection *connection=new UartTlsConnection(epoch);
    if(!connection){connection_state="allocation_failed";xSemaphoreGive(client_lock);return NULL;}
    connection_state="created";
    return connection;
}
void https_management_init()
{
    if(client_lock)return;
    replies=xQueueCreate(2,sizeof(Frame));requests=xQueueCreate(2,sizeof(Frame));
    client_lock=xSemaphoreCreateMutex();
    if(!replies || !requests || !client_lock || xTaskCreate(socket_worker,"TLS sockets",2048,NULL,tskIDLE_PRIORITY+1,NULL)!=pdPASS) {
        if(replies)vQueueDelete(replies);if(requests)vQueueDelete(requests);if(client_lock)vSemaphoreDelete(client_lock);
        replies=requests=NULL;client_lock=NULL;return;
    }
    http_set_secure_connection_factory(create_connection);
    worker_ready=true;
}
