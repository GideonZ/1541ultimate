// BSD socket calls as modem.cc makes them. The test defines them over a scripted network.
#ifndef HOST_MODEM_SOCKET_H
#define HOST_MODEM_SOCKET_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
extern "C" {
int host_modem_socket(int, int, int);
int host_modem_connect(int, const struct sockaddr *, socklen_t);
int host_modem_closesocket(int);
}
#define socket host_modem_socket
#define connect host_modem_connect
#define closesocket host_modem_closesocket
#define recv host_modem_recv
#define send host_modem_send
#define setsockopt host_modem_setsockopt
extern "C" {
ssize_t host_modem_recv(int, void *, size_t, int);
ssize_t host_modem_send(int, const void *, size_t, int);
int host_modem_setsockopt(int, int, int, const void *, socklen_t);
}
#endif
