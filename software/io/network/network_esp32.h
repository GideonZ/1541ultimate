/*
 * network_esp32.h
 *
 *  Created on: Nov 11, 2021
 *      Author: gideon
 */

#ifndef SOFTWARE_IO_NETWORK_NETWORK_ESP32_H_
#define SOFTWARE_IO_NETWORK_NETWORK_ESP32_H_

#include <socket.h>
#include <FreeRTOS.h>

int wifi_setbaud(int baudrate, uint8_t flowctrl);
BaseType_t wifi_detect(uint16_t *major, uint16_t *minor, char *str, int maxlen);
int wifi_getmac(uint8_t *mac);
int wifi_scan(void *);
int wifi_wifi_connect(char *ssid, char *password, uint8_t auth);
int wifi_wifi_disconnect();

extern "C" {
#include "cmd_buffer.h"
int wifi_socket(int domain, int type, int protocol);
int wifi_connect(int s, const struct sockaddr *name, socklen_t namelen);
int wifi_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int wifi_bind(int s, const struct sockaddr *name, socklen_t namelen);
int wifi_shutdown(int s, int how);
int wifi_close(int s);
int wifi_listen(int s, int backlog);
int wifi_read(int s, void *mem, size_t len);
int wifi_recv(int s, void *mem, size_t len, int flags);
int wifi_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
int wifi_write(int s, const void *dataptr, size_t size);
int wifi_send(int s, const void *dataptr, size_t size, int flags);
int wifi_sendto(int s, const void *dataptr, size_t size, int flags, const struct sockaddr *to, socklen_t tolen);
int wifi_getpeername (int s, struct sockaddr *name, socklen_t *namelen);
int wifi_getsockname (int s, struct sockaddr *name, socklen_t *namelen);
int wifi_getsockopt (int s, int level, int optname, void *optval, socklen_t *optlen);
int wifi_setsockopt (int s, int level, int optname, const void *optval, socklen_t optlen);
int wifi_ioctl(int s, long cmd, void *argp);
int wifi_fcntl(int s, int cmd, int val);
int wifi_gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen, struct hostent **result, int *h_errnop);

}
//int lwip_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset,
//                struct timeval *timeout);




#endif /* SOFTWARE_IO_NETWORK_NETWORK_ESP32_H_ */
