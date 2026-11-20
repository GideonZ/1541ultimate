// Host stand-in for lwIP's netdb.h: the host's struct hostent, and lwIP's
// gethostbyname_r, which net_tls_test.cc fakes.
#ifndef NETDB_H_NETSTUB
#define NETDB_H_NETSTUB
#include_next <netdb.h>
int gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen, struct hostent **result,
                    int *h_errnop);
#endif
