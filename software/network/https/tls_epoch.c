#include "tls_epoch.h"
#include "tls_wire.h"
#include <string.h>

static bool decode(const uint8_t *p, uint32_t *value)
{
    *value=tls_wire_get32(p+8);
    return !memcmp(p,"UTEPOCH2",8) && tls_wire_get32(p+12)==~*value;
}
static void encode(uint8_t *p,uint32_t value)
{
    memcpy(p,"UTEPOCH2",8);
    tls_wire_put32(p+8,value);tls_wire_put32(p+12,~value);
}
bool tls_epoch_advance(const tls_epoch_store *store,uint32_t *boot)
{
    uint8_t records[2][TLS_EPOCH_RECORD_SIZE],check[TLS_EPOCH_RECORD_SIZE];
    uint32_t values[2]={0,0};int present[2];
    *boot=0;
    for(unsigned i=0;i<2;i++) {
        present[i]=store->load(store->context,i,records[i]);
        if(present[i]<0 || (present[i] && !decode(records[i],&values[i])))return false;
    }
    /* A missing member of an existing pair is a storage failure, not permission
     * to start over. Bootstrap writes both zero records before issuing any ID. */
    if(present[0]!=present[1])return false;
    if(!present[0]) {
        encode(records[0],0);
        for(unsigned i=0;i<2;i++) {
            if(!store->save(store->context,i,records[0]) ||
               store->load(store->context,i,check)!=1 ||
               memcmp(check,records[0],sizeof check))return false;
        }
    }
    unsigned target=values[0]<=values[1]?0:1;
    uint32_t latest=values[0]>values[1]?values[0]:values[1];
    if(latest==UINT32_MAX)return false;
    encode(records[target],latest+1);
    if(!store->save(store->context,target,records[target]) ||
       store->load(store->context,target,check)!=1 ||
       memcmp(check,records[target],sizeof check))return false;
    *boot=latest+1;
    return true;
}
bool tls_epoch_next(uint32_t boot,uint32_t *counter,uint64_t *epoch)
{
    *epoch=0;
    if(!boot || *counter==UINT32_MAX)return false;
    *epoch=((uint64_t)boot<<32)|++*counter;
    return true;
}
