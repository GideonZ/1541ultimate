#ifndef HOST_MODEM_NETDB_H
#define HOST_MODEM_NETDB_H
#include_next <netdb.h>
#include <sys/socket.h>
extern "C" int host_modem_gethostbyname_r(const char *, struct hostent *, char *, size_t, struct hostent **, int *);
#define gethostbyname_r host_modem_gethostbyname_r
#endif
