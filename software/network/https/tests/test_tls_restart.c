#include "tls_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t bytes[TLS_WIRE_HEADER+TLS_WIRE_CHUNK]; size_t size; } frame;
static unsigned opens,closes;
static int open_stream(void *p,uint32_t job,uint64_t epoch,const char *host,uint16_t port,uint32_t ms)
{ (void)p;(void)job;assert(epoch && !strcmp(host,"example.test") && port==443 && ms);opens++;return 0; }
static int read_stream(void *p,uint8_t *bytes,size_t n)
{ (void)p;memset(bytes,'x',n);return (int)n; }
static int write_stream(void *p,const uint8_t *bytes,size_t n)
{ (void)p;(void)bytes;return (int)n; }
static void close_stream(void *p) { (void)p;closes++; }
static frame request(int op,uint32_t session,uint32_t sequence,uint64_t epoch)
{
    frame f={{'U','S',TLS_WIRE_VERSION,TLS_WIRE_REQUEST,TLS_WIRE_TLS,0},TLS_WIRE_HEADER};
    uint8_t *p=f.bytes;p[5]=(uint8_t)op;
    tls_wire_put32(p+8,session);tls_wire_put32(p+12,sequence);
    tls_wire_put16(p+16,1000);tls_wire_put_epoch(p,epoch);
    if(op==TLS_WIRE_OPEN) {
        tls_wire_put16(p+18,14);tls_wire_put16(p+TLS_WIRE_HEADER,443);
        memcpy(p+TLS_WIRE_HEADER+2,"example.test",12);f.size+=14;
    } else if(op==TLS_WIRE_READ)tls_wire_put16(p+18,8);
    assert(tls_wire_valid(p,f.size));return f;
}
static frame handle(tls_service *s,frame q)
{
    frame r;
    r.size=tls_service_handle(s,q.bytes,q.size,r.bytes,0,1);
    assert(tls_wire_matches(q.bytes,q.size,r.bytes,r.size));return r;
}
static bool matches(frame q,frame r)
{ return tls_wire_matches(q.bytes,q.size,r.bytes,r.size); }
static int result(frame r) { return (int32_t)tls_wire_get32(r.bytes+20); }
int main(void)
{
    tls_service s;tls_service_port port={NULL,open_stream,read_stream,write_stream,close_stream};
    const uint64_t old_epoch=UINT64_C(0x100000001),new_epoch=UINT64_C(0x200000001);
    tls_service_init(&s,&port,100);
    frame old_reserve=request(TLS_WIRE_RESERVE,UINT32_MAX,1,old_epoch);
    frame old_grant=handle(&s,old_reserve);
    uint32_t old_token=tls_wire_get32(old_grant.bytes+TLS_WIRE_HEADER);
    frame old_open=request(TLS_WIRE_OPEN,old_token,2,old_epoch);
    frame old_open_reply=handle(&s,old_open);assert(!result(old_open_reply));
    frame old_read=request(TLS_WIRE_READ,old_token,3,old_epoch);
    frame old_read_reply=handle(&s,old_read);assert(result(old_read_reply)==8);
    /* Reproduce the former bug: ignoring the epoch makes the old grant look
     * exactly like a reply to the restarted manager's first reserve request. */
    frame new_reserve=request(TLS_WIRE_RESERVE,UINT32_MAX,1,new_epoch);
    assert(!memcmp(old_reserve.bytes,new_reserve.bytes,24));
    assert(!matches(new_reserve,old_grant));
    frame new_grant=handle(&s,new_reserve);assert(!result(new_grant) && closes==1);
    uint32_t new_token=tls_wire_get32(new_grant.bytes+TLS_WIRE_HEADER);
    frame new_open=request(TLS_WIRE_OPEN,new_token,2,new_epoch);
    assert(!result(handle(&s,new_open)));
    assert(result(handle(&s,new_reserve))<0 && closes==1);
    assert(result(handle(&s,old_reserve))<0 && closes==1);
    assert(result(handle(&s,old_open))<0 && opens==2);
    assert(result(handle(&s,request(TLS_WIRE_CLOSE,new_token,99,old_epoch)))<0 && closes==1);
    assert(result(handle(&s,request(TLS_WIRE_READ,new_token,3,new_epoch)))==8);
    tls_service_expire(&s,2000);assert(opens==closes);

    /* Independent controller restart deliberately reuses the same seed/token.
     * A new client exchange epoch still rejects every recorded success. */
    tls_service_init(&s,&port,100);
    const uint64_t next_epoch=new_epoch+1;
    frame after_reserve=request(TLS_WIRE_RESERVE,UINT32_MAX,1,next_epoch);
    frame after_grant=handle(&s,after_reserve);
    assert(tls_wire_get32(after_grant.bytes+TLS_WIRE_HEADER)==old_token);
    frame after_open=request(TLS_WIRE_OPEN,old_token,2,next_epoch);
    assert(!matches(after_open,old_open_reply));
    assert(!result(handle(&s,after_open)));
    frame after_read=request(TLS_WIRE_READ,old_token,3,next_epoch);
    assert(!matches(after_read,old_read_reply));
    assert(result(handle(&s,old_read))<0);
    assert(result(handle(&s,after_read))==8);
    /* TCP/time replies use the same mandatory epoch matching rule. */
    old_read.bytes[4]=old_read_reply.bytes[4]=after_read.bytes[4]=TLS_WIRE_TCP;
    assert(matches(old_read,old_read_reply) && !matches(after_read,old_read_reply));
    tls_service_expire(&s,2000);assert(opens==closes);
    puts("TLS restart: stale grant/open/read/close, delayed reserve, repeated controller seed and TCP reply replay rejected.");
}
