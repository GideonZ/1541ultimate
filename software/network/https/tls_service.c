#include "tls_service.h"
#include "https_client.h"
#include <string.h>

static void release(tls_service *s)
{
    if (s->session) {
        s->port.close(s->port.context);
        s->retired = s->session;
        s->session = 0;
    }
}
void tls_service_init(tls_service *s, const tls_service_port *port, uint32_t seed)
{
    memset(s, 0, sizeof(*s)); s->port = *port;
    s->next_session=seed && seed<UINT32_MAX-1 ? seed : 1;
}
void tls_service_expire(tls_service *s, int64_t now)
{
    if (s->session && now >= s->deadline) release(s);
    if (s->reserved && now >= s->deadline) s->reserved=0;
}
size_t tls_service_handle(tls_service *s, const uint8_t *request, size_t size,
                          uint8_t *reply, int64_t queued, int64_t now)
{
    if (!tls_wire_valid(request, size) || request[3] != TLS_WIRE_REQUEST ||
        request[4] != TLS_WIRE_TLS) return 0;
    uint32_t job=tls_wire_get32(request+8), seq=tls_wire_get32(request+12);
    uint64_t epoch=tls_wire_get_epoch(request);
    unsigned op=request[5], count=tls_wire_get16(request+18);
    uint32_t timeout=tls_wire_get16(request+16);
    int result=-HTTPS_INVALID_ARGUMENT;
    size_t payload=0;
    /* Preserve data before writing reply metadata when the buffers alias. */
    memmove(reply,request,size);
    tls_service_expire(s,now);
    if (op == TLS_WIRE_CLOSE && epoch==s->epoch && job == s->retired && !s->session) result=0;
    else if (op == TLS_WIRE_CLOSE && epoch==s->epoch && job == s->session && seq > s->sequence) {
        release(s); result=0; /* Cleanup remains valid after a queue delay. */
    } else if (queued < 0 || now < queued || now > INT64_MAX-timeout ||
               now-queued >= timeout) result=-HTTPS_TIMEOUT;
    else if(op == TLS_WIRE_RESERVE && epoch>s->epoch) {
        /* A new management instance retires any old connection before it
         * learns its new token. Delayed OPENs cannot use a consumed grant. */
        release(s);s->reserved=0;s->epoch=epoch;
        if(s->next_session<UINT32_MAX-1) {
            s->reserved=++s->next_session;s->deadline=now+timeout;
            tls_wire_put32(reply+TLS_WIRE_HEADER,s->reserved);payload=4;result=0;
        }
    } else if (op == TLS_WIRE_OPEN) {
        if (epoch==s->epoch && !s->session && job == s->reserved && job != s->retired) {
            s->reserved=0;
            char host[HTTPS_MAX_HOST+1];
            memcpy(host,reply+TLS_WIRE_HEADER+2,count-2); host[count-2]=0;
            uint32_t left=timeout-(uint32_t)(now-queued);
            result=s->port.open(s->port.context,job,epoch,host,tls_wire_get16(reply+TLS_WIRE_HEADER),left);
            if (!result) {
                s->session=job; s->sequence=seq; s->deadline=now+left;
            } else s->retired=job;
        }
    } else if (epoch==s->epoch && job == s->session && seq > s->sequence) {
        s->sequence=seq;
        if (op == TLS_WIRE_READ) {
            result=s->port.read(s->port.context,reply+TLS_WIRE_HEADER,count);
            if (result>=0 && (unsigned)result<=count) payload=(size_t)result;
            else if (result>=0) result=-HTTPS_PROTOCOL_ERROR;
        } else if (op == TLS_WIRE_WRITE) {
            result=s->port.write(s->port.context,reply+TLS_WIRE_HEADER,count);
            if (result<=0 || (unsigned)result>count) result=-HTTPS_TRANSPORT_ERROR;
        }
        if (result<0) release(s);
    }
    reply[3]=TLS_WIRE_REPLY;
    tls_wire_put16(reply+16,0); tls_wire_put16(reply+18,(uint16_t)payload);
    tls_wire_put32(reply+20,(uint32_t)result);
    return TLS_WIRE_HEADER+payload;
}
