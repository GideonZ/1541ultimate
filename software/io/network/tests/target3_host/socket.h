// lwIP's socket API as the network target uses it, on the host's own sockets, for
// network_target_test.cpp.
#ifndef TARGET3_HOST_SOCKET_H
#define TARGET3_HOST_SOCKET_H
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#undef h_errno                        // open_socket declares a local of that name
#define lwip_close close
#define lwip_send send
#define lwip_recvmsg recvmsg
#define lwip_getsockopt getsockopt
#define PP_HTONS htons
#define sin_len sin_zero[0]           // the host's sockaddr_in has no length field
#define socket(d, t, p) ::socket(d, t, p)   // open_socket names a local `socket`
#endif
