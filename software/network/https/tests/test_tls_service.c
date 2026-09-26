#include "tls_service.h"
#include "https_client.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static uint64_t epoch=UINT64_C(0x100000001);
static int opens, closes, reads, writes, fail;
static int open_session(void *p,uint32_t job,uint64_t generation,const char *host,uint16_t port,uint32_t ms)
{
    (void)p; assert(generation==epoch); assert(job && !strcmp(host,"example.test") && port==443 && ms<=1000);
    opens++; return 0;
}
static int read_session(void *p,uint8_t *bytes,size_t n)
{ (void)p; reads++; if(fail)return -HTTPS_TRANSPORT_ERROR; memset(bytes,42,n); return (int)n; }
static int write_session(void *p,const uint8_t *bytes,size_t n)
{ (void)p;(void)bytes;writes++;return (int)n; }
static void close_session(void *p) { (void)p;closes++; }
static size_t request(uint8_t *p,int op,uint32_t job,uint32_t seq)
{
    memset(p,0,TLS_WIRE_HEADER+TLS_WIRE_CHUNK);
    p[0]='U';p[1]='S';p[2]=TLS_WIRE_VERSION;p[3]=TLS_WIRE_REQUEST;p[4]=TLS_WIRE_TLS;p[5]=(uint8_t)op;
    tls_wire_put_epoch(p,epoch);
    tls_wire_put32(p+8,job);tls_wire_put32(p+12,seq);tls_wire_put16(p+16,1000);
    if(op==TLS_WIRE_OPEN) {
        tls_wire_put16(p+18,14);tls_wire_put16(p+TLS_WIRE_HEADER,443);memcpy(p+(TLS_WIRE_HEADER+2),"example.test",12);return (TLS_WIRE_HEADER+14);
    }
    if(op==TLS_WIRE_READ || op==TLS_WIRE_WRITE) tls_wire_put16(p+18,8);
    return op==TLS_WIRE_WRITE ? (TLS_WIRE_HEADER+8) : TLS_WIRE_HEADER;
}
static int call(tls_service *s,int op,uint32_t job,uint32_t seq,int64_t queued,int64_t now)
{
    uint8_t p[TLS_WIRE_HEADER+TLS_WIRE_CHUNK],original[sizeof p];
    size_t n=request(p,op,job,seq);memcpy(original,p,n);
    size_t reply=tls_service_handle(s,p,n,p,queued,now);
    assert(reply && tls_wire_matches(original,n,p,reply));
    return (int32_t)tls_wire_get32(p+20);
}
static uint32_t reserve(tls_service *s,int64_t now)
{
    uint8_t p[TLS_WIRE_HEADER+TLS_WIRE_CHUNK],original[sizeof p];
    ++epoch;
    size_t n=request(p,TLS_WIRE_RESERVE,UINT32_MAX,1);memcpy(original,p,n);
    size_t reply=tls_service_handle(s,p,n,p,now,now);
    assert(reply==(TLS_WIRE_HEADER+4) && tls_wire_matches(original,n,p,reply));
    assert(!tls_wire_get32(p+20));
    return tls_wire_get32(p+TLS_WIRE_HEADER);
}
int main(void)
{
    tls_service s;tls_service_port port={NULL,open_session,read_session,write_session,close_session};
    tls_service_init(&s,&port,100);
    assert(call(&s,TLS_WIRE_RESERVE,UINT32_MAX,1,INT64_MAX-1,INT64_MAX-1)==-HTTPS_TIMEOUT);
    assert(call(&s,TLS_WIRE_OPEN,1,1,0,1000)==-HTTPS_TIMEOUT && !opens);
    assert(call(&s,TLS_WIRE_OPEN,1,1,1000,1001)<0 && !opens);
    uint32_t first=reserve(&s,1000);assert(first==101);
    assert(!call(&s,TLS_WIRE_OPEN,first,2,1000,1001));
    assert(call(&s,TLS_WIRE_OPEN,2,1,1000,1002)<0 && opens==1);
    assert(call(&s,TLS_WIRE_WRITE,first,3,1000,1003)==8 && writes==1);
    assert(call(&s,TLS_WIRE_WRITE,first,3,1000,1004)<0 && writes==1);
    assert(call(&s,TLS_WIRE_READ,2,3,1000,1005)<0 && !reads);
    assert(call(&s,TLS_WIRE_READ,first,4,1000,1006)==8 && reads==1);
    fail=1;assert(call(&s,TLS_WIRE_READ,first,5,1000,1007)<0 && closes==1);fail=0;
    assert(!call(&s,TLS_WIRE_CLOSE,first,6,0,3000) && closes==1);
    assert(call(&s,TLS_WIRE_OPEN,first,1,3000,3001)<0 && opens==1);
    uint32_t second=reserve(&s,3000);assert(second!=first);
    assert(!call(&s,TLS_WIRE_OPEN,second,2,3000,3001));
    tls_service_expire(&s,4000);assert(closes==2);
    assert(!call(&s,TLS_WIRE_CLOSE,second,3,0,5000) && closes==2);
    uint32_t third=reserve(&s,5000);
    assert(!call(&s,TLS_WIRE_OPEN,third,2,5000,5001));
    /* A restarted manager starts its sequence at one; the controller retires
     * its previous connection and grants a different, single-use token. */
    uint32_t fourth=reserve(&s,5002);assert(fourth!=third && closes==3);
    assert(call(&s,TLS_WIRE_OPEN,third,2,5002,5003)<0);
    assert(call(&s,TLS_WIRE_READ,third,3,5002,5003)<0 && reads==2);
    assert(!call(&s,TLS_WIRE_OPEN,fourth,2,5002,5003));
    assert(!call(&s,TLS_WIRE_CLOSE,fourth,3,0,5004) && closes==4);
    assert(call(&s,TLS_WIRE_OPEN,fourth,2,5004,5005)<0);
    uint32_t stale=reserve(&s,6000),fresh=reserve(&s,6001);
    assert(stale!=fresh && call(&s,TLS_WIRE_OPEN,stale,2,6001,6002)<0);
    tls_service_expire(&s,7001);
    assert(call(&s,TLS_WIRE_OPEN,fresh,2,7001,7002)<0);
    /* Exhaustion fails closed instead of wrapping into old session tokens. */
    tls_service_init(&s,&port,UINT32_MAX-2);
    assert(reserve(&s,8000)==UINT32_MAX-1);
    assert(call(&s,TLS_WIRE_RESERVE,UINT32_MAX,1,8001,8001)<0);
    assert(opens==closes);
    puts("TLS service: ownership, restart reservations, stale requests, deadlines, cleanup and exhaustion passed.");
    return 0;
}
