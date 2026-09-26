#include "tls_metrics.h"
#include "https_failure.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    tls_metrics source={{0},2}, decoded, unchanged;
    uint8_t storage[TLS_METRICS_SIZE+2], bad[TLS_METRICS_SIZE+1];
    memset(storage,0xbd,sizeof storage);
    memset(&unchanged,0xa5,sizeof unchanged);
    for(unsigned i=0;i<TLS_METRIC_COUNT;++i) source.value[i]=0x01020304u+i*0x11111111u;
    source.value[TLS_METRIC_BOOT_ID]=UINT32_MAX;
    source.value[TLS_METRIC_SEQUENCE]=0;
    tls_metrics_encode(storage+1,&source);
    assert(storage[0]==0xbd && storage[sizeof storage-1]==0xbd);
    assert(!memcmp(storage+1,"TM\2\0\4\3\2\1",8));
    assert(tls_metrics_decode(&decoded,storage+1,TLS_METRICS_SIZE));
    assert(!memcmp(&decoded,&source,sizeof source));
    for(size_t n=0;n<TLS_METRICS_SIZE;++n) {
        decoded=unchanged;
        assert(!tls_metrics_decode(&decoded,storage+1,n));
        assert(!memcmp(&decoded,&unchanged,sizeof decoded));
    }
    memcpy(bad,storage+1,TLS_METRICS_SIZE);
    assert(!tls_metrics_decode(&decoded,bad,sizeof bad));
    for(unsigned byte=0;byte<4;++byte) {
        for(unsigned value=0;value<256;++value) {
            if(value==storage[1+byte]) continue;
            memcpy(bad,storage+1,TLS_METRICS_SIZE);
            bad[byte]=(uint8_t)value; decoded=unchanged;
            assert(!tls_metrics_decode(&decoded,bad,TLS_METRICS_SIZE));
            assert(!memcmp(&decoded,&unchanged,sizeof decoded));
        }
    }
    assert(!tls_metrics_decode(NULL,storage+1,TLS_METRICS_SIZE));
    assert(!tls_metrics_decode(&decoded,NULL,TLS_METRICS_SIZE));
    /* New management still accepts old heap-only samples without inventing
     * TLS failures. A version mismatch at the new length remains rejected. */
    memcpy(bad,storage+1,TLS_METRICS_SIZE); bad[2]=1;
    assert(tls_metrics_decode(&decoded,bad,4+4*TLS_METRICS_V1_COUNT));
    assert(decoded.version==1);
    for(unsigned i=0;i<TLS_METRIC_COUNT;++i)
        assert(decoded.value[i]==(i<TLS_METRICS_V1_COUNT?source.value[i]:0));
    https_failure fault={0};
    https_failure_record(&fault,0,1,"invalid",-1,0);
    assert(!fault.count);
    https_failure_record(&fault,UINT64_C(0x100000002),19,"tcp_read",104,0);
    https_failure_record(&fault,UINT64_C(0x100000002),19,"tls_reply",-4,3);
    https_failure_record(&fault,UINT64_C(0x100000001),18,"late_work",-1,0);
    assert(fault.count==1 && fault.code==104 && !strcmp(fault.stage,"tcp_read"));
    https_failure_record(&fault,UINT64_C(0x200000001),20,"http_framing",7,12);
    assert(fault.count==2 && fault.session==20 && fault.detail==12);
    fault.count=INT32_MAX;
    https_failure_record(&fault,UINT64_C(0x200000002),21,"uart_deadline",0,2);
    assert(fault.count==INT32_MAX && fault.session==21);
    puts("TLS metrics: endian roundtrip, boundary values, canaries, truncation, header rejection and unchanged-output checks passed.");
    return 0;
}
