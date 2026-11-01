// Included by the embedded runtime fixture after its common helpers.
struct RenewalPort : ControlPort {
    unsigned accepted=0, rejected=0, closes=0;
    bool send(const uint8_t *p,size_t size) override {
        if (!ControlPort::send(p,size)) return false;
        if (response.opcode==provider_control::Renew) {
            if (response.status==provider_control::Ok) ++accepted; else ++rejected;
        }
        if (response.opcode==provider_control::Close) ++closes;
        return true;
    }
};
struct RenewalActor {
    provider_control::Worker& worker;
    provider_session::Grant grant;
    TaskHandle_t waiter;
    unsigned mode;
    uint32_t stop=0;
    unsigned sent=0, failed=0;
    uint64_t signal_ms=0;
};
static void renew_during_call(void *context) {
    auto& actor=*static_cast<RenewalActor*>(context);
    uint64_t start=now_ms(); unsigned sequence=0;
    while (!__atomic_load_n(&actor.stop,__ATOMIC_ACQUIRE)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (now_ms()-start<(sequence+1)*500 || __atomic_load_n(&actor.stop,__ATOMIC_ACQUIRE)) continue;
        ++sequence;
        if (actor.mode==2) continue; // Deliberately no renewals.
        provider_control::Frame f={}; f.opcode=provider_control::Renew;
        f.request=10+sequence; f.grant=actor.grant; f.counter=actor.mode==3 ? 1 : sequence;
        if (sequence==3 && actor.mode>=4) {
            actor.signal_ms=now_ms();
            if (actor.mode==4) { f.opcode=provider_control::Close; f.counter=1; }
            else { f={}; f.opcode=provider_control::Offer; f.request=99; f.grant.owner.bytes[0]=3; }
        }
        uint8_t bytes[provider_control::FrameSize]; provider_control::encode(f,bytes);
        ++actor.sent; if (!actor.worker.submit(bytes,sizeof(bytes))) ++actor.failed;
        if (actor.signal_ms) break;
    }
    xTaskNotifyGive(actor.waiter); vTaskDelete(0);
}
static void long_call(unsigned mode) {
    static const char *names[]={"renewed-read","renewed-timeout","missing-renewal","replayed-renewal","renewed-close","renewed-offer"};
    printf("LONG_ATTEMPT name=%s run=%s\n",names[mode],LAB_RUN_ID);
    __atomic_store_n(&clock_ready_flag,uint32_t(1),__ATOMIC_RELEASE);
    __atomic_store_n(&entropy_ready_flag,uint32_t(1),__ATOMIC_RELEASE);
    EspProviderTlsPlatform platform;
    check(platform.configure(lab_root,sizeof(lab_root),clock_ready,entropy_ready),"long-call trust configured");
    RenewalPort port; check(port.queue!=0,"long-call queue created");
    provider_session::Identity boot={}; boot.bytes[0]=1;
    provider_control::Worker worker(port,boot,provider_session::Required,{1000,1500,1000});
    provider_data::TlsBackend backend(worker,platform);
    provider_control::Frame control={}; control.opcode=provider_control::Offer; control.request=1; control.grant.owner.bytes[0]=2;
    submit(worker,control); check(port.response.status==provider_control::Ok,"long-call offer accepted");
    control=port.response; control.reply=false; control.status=0; control.opcode=provider_control::Initialize; control.request=2; control.required=provider_session::Required;
    submit(worker,control); check(port.response.status==provider_control::Ok,"long-call grant initialized");
    provider_data::Frame f={}; f.opcode=provider_data::Open; f.request=1; f.ticket.grant=port.response.grant; f.ticket.session=1;
    f.port=mode==0 ? 8448 : 8447; f.budget_ms=mode==1 ? 3500 : 4000;
    const char host[]="tls.lab.test"; f.length=sizeof(host); memcpy(f.payload,host,sizeof(host));
    RenewalActor actor={worker,f.ticket.grant,xTaskGetCurrentTaskHandle(),mode};
    check(xTaskCreate(renew_during_call,"Lease ingress",2048,&actor,tskIDLE_PRIORITY+2,0)==pdPASS,"renewal actor started");
    uint64_t start=now_ms(); int result=backend.execute(f);
    check(result==0,"long-call authenticated open");
    const char request[]="GET /probe HTTP/1.1\r\nHost: tls.lab.test\r\nConnection: close\r\n\r\n";
    f.opcode=provider_data::Write; f.length=f.count=sizeof(request)-1; memcpy(f.payload,request,f.length);
    result=backend.execute(f); check(result==sizeof(request)-1,"long-call exact request sent");
    f.opcode=provider_data::Read; f.length=0; f.count=256;
    unsigned char response[256]={}; unsigned received=0;
    do {
        result=backend.execute(f);
        if (result>0 && received+result<=sizeof(response)) { memcpy(response+received,f.payload,result); received+=result; }
        else if (result>0) { check(false,"long-call response bounded"); break; }
    } while (result>0);
    unsigned elapsed=now_ms()-start; auto diagnostic=backend.diagnostic();
    __atomic_store_n(&actor.stop,uint32_t(1),__ATOMIC_RELEASE);
    bool joined=ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1000))>0; check(joined,"renewal actor joined"); configASSERT(joined);
    check(actor.failed==0,"control ingress accepted");
    if (mode==0) {
        const char header[]="HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\n";
        const unsigned char binary[]={0,255,128,13,10,'A',0,10};
        check(result==0 && received==sizeof(header)-1+8 && !memcmp(response,header,sizeof(header)-1) &&
            !memcmp(response+sizeof(header)-1,LAB_ECDSA ? binary : reinterpret_cast<const unsigned char*>("lwip-ok\n"),8),"renewed response and EOF exact");
        check(diagnostic.status==TLS_OK && elapsed>=2200 && elapsed<=4000 && port.accepted>=4,"renewal keeps long call alive");
    } else {
        check(result<0 && received==0,"failed long call publishes no response bytes");
        bool bounded=false;
        if (mode==1) bounded=diagnostic.status==TLS_TIMEOUT && elapsed>=3500 && elapsed<=3750 && port.accepted>=6;
        else if (mode==2) bounded=diagnostic.status==TLS_CANCELLED && elapsed>=1450 && elapsed<=1750 && !port.accepted;
        else if (mode==3) bounded=diagnostic.status==TLS_CANCELLED && elapsed>=1950 && elapsed<=2250 && port.accepted==1 && port.rejected>=1;
        else bounded=diagnostic.status==TLS_CANCELLED && actor.signal_ms && start+elapsed>=actor.signal_ms && start+elapsed-actor.signal_ms<=250 && port.accepted>=2;
        check(bounded,"original deadline and lease cancellation bounds");
    }
    check(worker.occupied() && backend.occupied() && !port.closes,"resource and close reply deferred until TLS unwinds");
    if (mode<4) worker.disconnect();
    worker.step(now_ms());
    check(!worker.occupied() && !backend.occupied(),"long-call owner cleanup complete");
    check(mode!=4 || port.closes==1,"close acknowledged after cleanup");
    check(uxTaskGetStackHighWaterMark(0)>=1024,"long-call stack reserve");
    printf("LONG_RESULT name=%s status=%d elapsed_ms=%u accepted=%u rejected=%u sent=%u signal_return=%u stack=%u run=%s\n",names[mode],int(diagnostic.status),elapsed,port.accepted,port.rejected,actor.sent,
        unsigned(actor.signal_ms ? start+elapsed-actor.signal_ms : 0),unsigned(uxTaskGetStackHighWaterMark(0)),LAB_RUN_ID);
}
