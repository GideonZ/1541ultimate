#include "tls_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void header(uint8_t *p, int type, int op, unsigned count)
{
    memset(p, 0, TLS_WIRE_HEADER + TLS_WIRE_CHUNK);
    p[0]='U'; p[1]='S'; p[2]=TLS_WIRE_VERSION; p[3]=(uint8_t)type; p[4]=TLS_WIRE_TCP; p[5]=(uint8_t)op;
    tls_wire_put_epoch(p,UINT64_C(0x100000001));
    tls_wire_put32(p+8, 0x12345678); tls_wire_put32(p+12, 0xabcdef01);
    tls_wire_put16(p+16, type == TLS_WIRE_REQUEST ? 1000 : 0);
    tls_wire_put16(p+18, (uint16_t)count);
}
int main(void)
{
    uint8_t req[TLS_WIRE_HEADER+TLS_WIRE_CHUNK], rep[sizeof req];
    header(req, TLS_WIRE_REQUEST, TLS_WIRE_OPEN, 14);
    tls_wire_put16(req+TLS_WIRE_HEADER, 8443); memcpy(req+(TLS_WIRE_HEADER+2),"example.test",12);
    assert(tls_wire_valid(req,(TLS_WIRE_HEADER+14)));
    for(size_t n=0;n<(TLS_WIRE_HEADER+14);n++) assert(!tls_wire_valid(req,n));
    req[(TLS_WIRE_HEADER+2)]='@'; assert(!tls_wire_valid(req,(TLS_WIRE_HEADER+14))); req[(TLS_WIRE_HEADER+2)]='e';
    req[(TLS_WIRE_HEADER+3)]=0; assert(!tls_wire_valid(req,(TLS_WIRE_HEADER+14))); req[(TLS_WIRE_HEADER+3)]='x';
    header(rep,TLS_WIRE_REPLY,TLS_WIRE_OPEN,0);
    assert(tls_wire_matches(req,(TLS_WIRE_HEADER+14),rep,TLS_WIRE_HEADER));
    rep[12]++; assert(!tls_wire_matches(req,(TLS_WIRE_HEADER+14),rep,TLS_WIRE_HEADER)); rep[12]--;
    rep[8]++; assert(!tls_wire_matches(req,(TLS_WIRE_HEADER+14),rep,TLS_WIRE_HEADER)); rep[8]--;
    rep[4]=TLS_WIRE_TLS; assert(!tls_wire_matches(req,(TLS_WIRE_HEADER+14),rep,TLS_WIRE_HEADER));
    header(req,TLS_WIRE_REQUEST,TLS_WIRE_READ,7);
    header(rep,TLS_WIRE_REPLY,TLS_WIRE_READ,7); tls_wire_put32(rep+20,7);
    assert(tls_wire_matches(req,TLS_WIRE_HEADER,rep,(TLS_WIRE_HEADER+7)));
    tls_wire_put16(req+18,6); assert(!tls_wire_matches(req,TLS_WIRE_HEADER,rep,(TLS_WIRE_HEADER+7)));
    tls_wire_put32(rep+20,UINT32_MAX); tls_wire_put16(rep+18,0);
    assert(tls_wire_matches(req,TLS_WIRE_HEADER,rep,TLS_WIRE_HEADER)); assert(!tls_wire_valid(rep,(TLS_WIRE_HEADER+1)));
    header(req,TLS_WIRE_REQUEST,TLS_WIRE_WRITE,512);
    assert(tls_wire_valid(req,sizeof req));
    header(rep,TLS_WIRE_REPLY,TLS_WIRE_WRITE,0); tls_wire_put32(rep+20,512);
    assert(tls_wire_matches(req,sizeof req,rep,TLS_WIRE_HEADER));
    tls_wire_put32(rep+20,513); assert(!tls_wire_valid(rep,TLS_WIRE_HEADER));
    header(req,TLS_WIRE_REQUEST,TLS_WIRE_TIME,0);
    header(rep,TLS_WIRE_REPLY,TLS_WIRE_TIME,8);
    assert(tls_wire_matches(req,TLS_WIRE_HEADER,rep,(TLS_WIRE_HEADER+8)));
    req[4]=TLS_WIRE_TLS; assert(!tls_wire_valid(req,TLS_WIRE_HEADER));
    header(req,TLS_WIRE_REQUEST,TLS_WIRE_RESERVE,0);
    req[4]=TLS_WIRE_TLS;tls_wire_put32(req+8,UINT32_MAX);
    header(rep,TLS_WIRE_REPLY,TLS_WIRE_RESERVE,4);
    rep[4]=TLS_WIRE_TLS;tls_wire_put32(rep+8,UINT32_MAX);tls_wire_put32(rep+TLS_WIRE_HEADER,42);
    assert(tls_wire_matches(req,TLS_WIRE_HEADER,rep,(TLS_WIRE_HEADER+4)));
    for(size_t n=TLS_WIRE_HEADER;n<(TLS_WIRE_HEADER+4);n++) assert(!tls_wire_valid(rep,n));
    tls_wire_put32(rep+TLS_WIRE_HEADER,0);assert(!tls_wire_valid(rep,(TLS_WIRE_HEADER+4)));
    tls_wire_put32(rep+TLS_WIRE_HEADER,UINT32_MAX);assert(!tls_wire_valid(rep,(TLS_WIRE_HEADER+4)));
    tls_wire_put32(rep+TLS_WIRE_HEADER,42);rep[12]++;assert(!tls_wire_matches(req,TLS_WIRE_HEADER,rep,(TLS_WIRE_HEADER+4)));
    req[4]=TLS_WIRE_TCP;assert(!tls_wire_valid(req,TLS_WIRE_HEADER));req[4]=TLS_WIRE_TLS;
    tls_wire_put32(req+8,42);assert(!tls_wire_valid(req,TLS_WIRE_HEADER));
    header(req,TLS_WIRE_REQUEST,TLS_WIRE_CLOSE,0);
    assert(tls_wire_valid(req,TLS_WIRE_HEADER));
    tls_wire_put_epoch(req,1);assert(!tls_wire_valid(req,TLS_WIRE_HEADER));
    tls_wire_put_epoch(req,UINT64_C(0x100000000));assert(!tls_wire_valid(req,TLS_WIRE_HEADER));
    tls_wire_put_epoch(req,UINT64_C(0x100000001));
    req[6]=1; assert(!tls_wire_valid(req,TLS_WIRE_HEADER)); req[6]=0;
    req[2]=1; assert(!tls_wire_valid(req,TLS_WIRE_HEADER)); req[2]=TLS_WIRE_VERSION;
    tls_wire_put16(req+16,15001); assert(!tls_wire_valid(req,TLS_WIRE_HEADER));
    tls_wire_put16(req+16,0); assert(!tls_wire_valid(req,TLS_WIRE_HEADER));
    assert(!tls_wire_valid(NULL,TLS_WIRE_HEADER));
    /* All truncated sizes and byte values are safe to reject/inspect. */
    for(size_t size=0;size<=sizeof req;size++) {
        for(unsigned value=0;value<256;value++) {
            memset(req,(int)value,sizeof req);
            (void)tls_wire_valid(req,size);
        }
    }
    puts("TLS wire: framing, bounds, generation/sequence matching and malformed input passed.");
    return 0;
}
