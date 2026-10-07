#include "tls_epoch.h"
#include "tls_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t bytes[2][TLS_EPOCH_RECORD_SIZE];
    bool exists[2],fail_load,fail_save,corrupt_readback;
    int cut;unsigned writes;
} disk;
static int load(void *context,unsigned slot,uint8_t *out)
{
    disk *d=context;
    if(d->fail_load)return -1;
    if(!d->exists[slot])return 0;
    memcpy(out,d->bytes[slot],TLS_EPOCH_RECORD_SIZE);
    if(d->corrupt_readback && d->writes)out[0]^=1;
    return 1;
}
static bool save(void *context,unsigned slot,const uint8_t *in)
{
    disk *d=context;d->writes++;
    if(d->fail_save)return false;
    size_t n=d->cut<0?TLS_EPOCH_RECORD_SIZE:(size_t)d->cut;
    memcpy(d->bytes[slot],in,n);d->exists[slot]=true;
    return d->cut<0;
}
int main(void)
{
    disk d={.cut=-1};tls_epoch_store store={&d,load,save};
    uint32_t boot=0,count=0;uint64_t first,next;
    assert(tls_epoch_advance(&store,&boot) && boot==1);
    assert(tls_epoch_next(boot,&count,&first));
    assert(tls_epoch_next(boot,&count,&next) && next!=first);
    /* Management restart resets RAM counters, but cannot reproduce an epoch. */
    count=0;assert(tls_epoch_advance(&store,&boot) && boot==2);
    assert(tls_epoch_next(boot,&count,&next) && next>first);
    disk baseline=d;
    /* Power loss at every byte of a write: no epoch is emitted on failure;
     * recovery either fails closed or advances beyond every previously used ID. */
    for(int cut=0;cut<=TLS_EPOCH_RECORD_SIZE;cut++) {
        d=baseline;d.cut=cut;boot=99;
        assert(!tls_epoch_advance(&store,&boot) && !boot);
        d.cut=-1;
        if(tls_epoch_advance(&store,&boot))assert(boot>2);
    }
    d=baseline;d.fail_load=true;assert(!tls_epoch_advance(&store,&boot) && !boot);
    d=baseline;d.fail_save=true;assert(!tls_epoch_advance(&store,&boot) && !boot);
    d=baseline;d.writes=0;d.corrupt_readback=true;
    assert(!tls_epoch_advance(&store,&boot) && !boot);
    d=baseline;d.exists[0]=false;assert(!tls_epoch_advance(&store,&boot) && !boot);
    d=baseline;d.bytes[0][12]^=1;assert(!tls_epoch_advance(&store,&boot) && !boot);
    for(unsigned slot=0;slot<2;slot++) {
        for(unsigned byte=0;byte<TLS_EPOCH_RECORD_SIZE;byte++) {
            d=baseline;d.bytes[slot][byte]^=1;
            assert(!tls_epoch_advance(&store,&boot) && !boot);
        }
    }
    /* A committed reservation whose readback fails is skipped after reboot. */
    d=baseline;d.writes=0;d.corrupt_readback=true;
    assert(!tls_epoch_advance(&store,&boot));d.corrupt_readback=false;
    assert(tls_epoch_advance(&store,&boot) && boot==4);
    /* Interrupted first-use provisioning never returns an epoch. */
    for(int cut=0;cut<=TLS_EPOCH_RECORD_SIZE;cut++) {
        memset(&d,0,sizeof d);d.cut=cut;
        assert(!tls_epoch_advance(&store,&boot) && !boot);
        d.cut=-1;assert(!tls_epoch_advance(&store,&boot) && !boot);
    }
    d=baseline;tls_wire_put32(d.bytes[0]+8,UINT32_MAX);tls_wire_put32(d.bytes[0]+12,0);
    assert(!tls_epoch_advance(&store,&boot) && !boot);
    count=UINT32_MAX;assert(!tls_epoch_next(1,&count,&next) && !next);
    count=0;assert(!tls_epoch_next(0,&count,&next) && !next);
    puts("TLS epoch: durable boot IDs, restart uniqueness, torn writes, I/O errors, corruption and exhaustion passed.");
}
