/* Peak of the MEMP_SYS_TIMEOUT pool with lwIP's real timeouts.c, mdns.c, sntp.c, tcp, dhcp, dns, igmp, etharp:
   N netifs, hostname only (as init_callback does, no mDNS service), driven by frames through netif->input.
   An empty pool asserts in sys_timeout(); the firmware's assert spins for ever in the calling task (mDNS input and the lwIP timers run in the tcpip thread, which holds the core lock), here it is counted.
   usage: timeout_pool_sim <N netifs> [tcp 0|1] [sntp 0|1]; exit status 1 when the pool ran empty.
   The pool size is the -DPOOL of the build: the firmware's MEMP_NUM_SYS_TIMEOUT (the Makefile reads it from
   lwipopts.h), or 16, the value before the fix. */
#ifdef CHECK_BOUND
#if POOL < 6 + 2 + 4 + 6 * 4
#error "POOL is below 6 lwIP + 2 SNTP + 4 truncated mDNS questions + 6 mDNS per netif for 4 netifs"
#endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/stats.h"
#include "lwip/memp.h"
#include "lwip/tcp.h"
#include "lwip/igmp.h"
#include "lwip/etharp.h"
#include "lwip/apps/mdns.h"
#include "lwip/apps/sntp.h"
#include "netif/ethernet.h"
int sim_asserts; const char *sim_last_assert;
static u32_t now_ms; u32_t sys_now(void) { return now_ms; }
static int used(void) { return lwip_stats.memp[MEMP_SYS_TIMEOUT]->used; }
static int peak;
static void tick(int ms) { for (int i = 0; i < ms; i += 5) { now_ms += 5; sys_check_timeouts(); if (used() > peak) peak = used(); } }
static err_t linkout(struct netif *n, struct pbuf *p) { (void)n; (void)p; return ERR_OK; }
static err_t igmpf(struct netif *n, const ip4_addr_t *g, enum netif_mac_filter_action a) { (void)n;(void)g;(void)a; return ERR_OK; }
static err_t nif_init(struct netif *n) {
  n->name[0]='U'; n->name[1]='2'; n->hwaddr_len=6; memset(n->hwaddr,0x02,6); n->hwaddr[5]=(u8_t)(n->num+1); n->mtu=1500;
  n->output=etharp_output; n->linkoutput=linkout; n->igmp_mac_filter=igmpf;
  n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_LINK_UP|NETIF_FLAG_IGMP|NETIF_FLAG_ETHERNET;
  return ERR_OK; }
static u16_t csum(const u8_t *d, int n) { u32_t s=0; for(int i=0;i+1<n;i+=2) s+=(d[i]<<8)|d[i+1]; while(s>>16) s=(s&0xFFFF)+(s>>16); return (u16_t)~s; }
/* a query for <name>.local type A, class IN, qu = QU bit, tc = truncated flag, from 10.0.<net>.<host>, multicast */
static int two;
static void query(struct netif *n, int net, int host, const char *name, int qu, int tc) {
  u8_t f[256]; memset(f,0,sizeof f); int o=0;
  static const u8_t mcmac[6]={1,0,0x5e,0,0,0xfb};
  memcpy(f,mcmac,6); f[6]=2;f[7]=0xAA;f[8]=0;f[9]=0;f[10]=(u8_t)net;f[11]=(u8_t)host; f[12]=8;f[13]=0; o=14;
  u8_t *ip=f+o; ip[0]=0x45; ip[8]=255; ip[9]=17; ip[12]=10;ip[13]=0;ip[14]=(u8_t)net;ip[15]=(u8_t)host; ip[16]=224;ip[17]=0;ip[18]=0;ip[19]=251; o+=20;
  u8_t *u=f+o; u[0]=0x14;u[1]=0xE9;u[2]=0x14;u[3]=0xE9; o+=8;
  u8_t *d=f+o; d[2]=tc?0x02:0; d[5]=two?2:1; int q=12; int l=(int)strlen(name); d[q++]=(u8_t)l; memcpy(d+q,name,l); q+=l; d[q++]=5; memcpy(d+q,"local",5); q+=5; d[q++]=0;
  d[q++]=0;d[q++]=1;d[q++]=qu?0x80:0;d[q++]=1; if(two){d[q++]=0xC0;d[q++]=12;d[q++]=0;d[q++]=28;d[q++]=qu?0x80:0;d[q++]=1;} o+=q;
  int udplen=8+q, iplen=20+udplen; ip[2]=(u8_t)(iplen>>8);ip[3]=(u8_t)iplen; u[4]=(u8_t)(udplen>>8);u[5]=(u8_t)udplen;
  u16_t c=csum(ip,20); ip[10]=(u8_t)(c>>8); ip[11]=(u8_t)c;
  struct pbuf *p=pbuf_alloc(PBUF_RAW,(u16_t)o,PBUF_RAM); memcpy(p->payload,f,o);
  if (n->input(p,n)!=ERR_OK) pbuf_free(p);
}
int main(int argc, char **argv) {
  int N = argc>1?atoi(argv[1]):1, withtcp = argc>2?atoi(argv[2]):1, withsntp = argc>3?atoi(argv[3]):1;
  static struct netif nif[4]; ip4_addr_t a,m,g;
  lwip_init(); mdns_resp_init();
  printf("after lwip_init: used=%d (cyclic)\n", used());
  for(int i=0;i<N;i++){ IP4_ADDR(&a,10,0,(u8_t)i,2); IP4_ADDR(&m,255,255,255,0); IP4_ADDR(&g,10,0,(u8_t)i,1);
    netif_add(&nif[i],&a,&m,&g,NULL,nif_init,ethernet_input); netif_set_up(&nif[i]);
    igmp_start(&nif[i]); mdns_resp_add_netif(&nif[i],"u64"); if(!i) netif_set_default(&nif[i]); }
  if (withsntp) { ip_addr_t s; IP4_ADDR(ip_2_ip4(&s),10,0,0,1); sntp_setoperatingmode(SNTP_OPMODE_POLL); sntp_setserver(0,&s); sntp_init(); }
  if (withtcp) { struct tcp_pcb *t=tcp_new(); ip_addr_t d; IP4_ADDR(ip_2_ip4(&d),10,0,0,9); tcp_connect(t,&d,80,NULL); }
  printf("configured: used=%d\n", used());
  tick(3000); printf("after probing/announce (3 s): used=%d peak=%d\n", used(), peak);
  tick(2000); int base=used(); printf("settled: used=%d\n", base);
  for (int r=0;r<3;r++) {
    two=1; for(int i=0;i<N;i++) query(&nif[i],i,12,"u64",1,0); /* two questions, QU: delayed unicast */
    for(int i=0;i<N;i++) query(&nif[i],i,13,"u64",0,0);        /* two questions: delayed multicast */
    printf(" r%d after 2-question QU+QM: used=%d\n",r,used()); two=0;
    for(int i=0;i<N;i++) query(&nif[i],i,10,"u64",0,0);   /* plain query: answered at once, starts the rate limit timers */
    printf(" r%d after QM: used=%d\n",r,used());
    tick(30);
    for(int i=0;i<N;i++) query(&nif[i],i,11,"u64",1,0);   /* QU */
    printf(" r%d after QU: used=%d\n",r,used());
    for(int k=0;k<6;k++) query(&nif[0],0,(20+k),"u64",0,1); /* truncated questions */
    printf(" r%d after TC x6: used=%d\n",r,used());
    if (used()>peak) peak=used();
    tick(40);
    if (used()>peak) peak=used();
    tick(1100);
  }
  printf("RESULT pool=%d N=%d tcp=%d sntp=%d peak=%d asserts=%d lwip_max=%d %s\n",POOL,N,withtcp,withsntp,peak,sim_asserts,lwip_stats.memp[MEMP_SYS_TIMEOUT]->max, sim_last_assert?sim_last_assert:"");
  return sim_asserts ? 1 : 0;
}
