/**
 * @file
 * This file is a posix wrapper for lwip/sockets.h.
 */

/*
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 */

#include "lwip/sockets.h"

#if LWIP_COMPAT_SOCKETS == 0
#include "network_esp32.h"

#define accept(a,b,c)         wifi_accept(a,b,c)
#define bind(a,b,c)           wifi_bind(a,b,c)
#define shutdown(a,b)         wifi_shutdown(a,b)
#define closesocket(s)        wifi_close(s)
#define connect(a,b,c)        wifi_connect(a,b,c)
#define getsockname(a,b,c)    wifi_getsockname(a,b,c)
#define getpeername(a,b,c)    wifi_getpeername(a,b,c)
#define setsockopt(a,b,c,d,e) wifi_setsockopt(a,b,c,d,e)
#define getsockopt(a,b,c,d,e) wifi_getsockopt(a,b,c,d,e)
#define listen(a,b)           wifi_listen(a,b)
#define recv(a,b,c,d)         wifi_recv(a,b,c,d)
#define recvfrom(a,b,c,d,e,f) wifi_recvfrom(a,b,c,d,e,f)
#define send(a,b,c,d)         wifi_send(a,b,c,d)
#define sendto(a,b,c,d,e,f)   wifi_sendto(a,b,c,d,e,f)
#define socket(a,b,c)         wifi_socket(a,b,c)
#define select(a,b,c,d,e)     wifi_select(a,b,c,d,e)
#define ioctlsocket(a,b,c)    wifi_ioctl(a,b,c)

//#define gethostbyname(name)                                          wifi_gethostbyname(name)
#define gethostbyname_r(name, ret, buf, buflen, result, h_errnop)    wifi_gethostbyname_r(name, ret, buf, buflen, result, h_errnop)


#if LWIP_POSIX_SOCKETS_IO_NAMES
#define __read(a,b,c)           wifi_read(a,b,c)
#define __write(a,b,c)          wifi_write(a,b,c)
#define __close(s)              wifi_close(s)
#define __fcntl(a,b,c)          wifi_fcntl(a,b,c)
#endif /* LWIP_POSIX_SOCKETS_IO_NAMES */

#endif /* LWIP_COMPAT_SOCKETS */
