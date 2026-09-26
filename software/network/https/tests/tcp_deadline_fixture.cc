#include "https_failure.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t TickType_t;
static uint32_t tick,tcp_owner,active_job,tcp_start,tcp_timeout,closed;
static uint64_t tcp_epoch,active_epoch;
static https_failure failure;
static uint32_t elapsed(TickType_t start) {return tick-start;}
static bool is_active(uint32_t job,uint64_t epoch) {return job==active_job && epoch==active_epoch;}
static void close_tcp() {tcp_owner=0;closed++;}
static void record_failure(uint64_t epoch,uint32_t job,const char *stage,int code,int detail=0)
{https_failure_record(&failure,epoch,job,stage,code,detail);}

// PRODUCTION_DEADLINE_DECISIONS

static void setup()
{
    tick=99;tcp_start=0;tcp_timeout=100;closed=0;
    tcp_owner=active_job=7;tcp_epoch=active_epoch=UINT64_C(0x100000001);
    memset(&failure,0,sizeof failure);
}
int main()
{
    setup();expire_tcp_owner();assert(tcp_owner==7 && !closed && !failure.count);
    tick=100;expire_tcp_owner();
    assert(!tcp_owner && closed==1 && failure.count==1);
    assert(!strcmp(failure.stage,"tcp_session_deadline") && failure.epoch==tcp_epoch && failure.session==7);
    record_failure(tcp_epoch,7,"tls_reply",-4,2);
    assert(!strcmp(failure.stage,"tcp_session_deadline"));
    expire_tcp_owner();assert(closed==1 && failure.count==1);

    setup();active_epoch++;tick=100;expire_tcp_owner();
    assert(closed==1 && !failure.count); // Cancellation is not a timeout.
    setup();tcp_owner=0;tick=100;expire_tcp_owner();assert(!closed && !failure.count);
    setup();tcp_start=UINT32_MAX-50;tick=49;expire_tcp_owner();
    assert(closed==1 && failure.count==1); // Unsigned clock wrap.

    setup();assert(tcp_request_live(7,tcp_epoch,0,100,2));assert(!failure.count);
    tick=100;assert(!tcp_request_live(7,tcp_epoch,0,100,2));
    assert(failure.count==1 && !strcmp(failure.stage,"tcp_queue_deadline") && failure.detail==2);
    assert(tcp_owner==7 && !closed); // The request check does not close a different socket.
    record_failure(tcp_epoch,7,"tls_reply",-4,2);assert(!strcmp(failure.stage,"tcp_queue_deadline"));
    setup();tick=100;assert(!tcp_request_live(8,tcp_epoch,0,100,2));assert(!failure.count);
    assert(!tcp_request_live(7,tcp_epoch-1,0,100,2));assert(!failure.count);
    assert(!tcp_request_live(7,tcp_epoch,100,0,3));assert(failure.count==1 && failure.detail==3);
    puts("TCP deadline decisions: session/queue expiry, boundaries, cancellation, stale ownership, wrap and first-cause preservation passed.");
}
