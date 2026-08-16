#ifndef SYSLOG_H
#define SYSLOG_H

#include "FreeRTOS.h"
#include "task.h"

#include <sys/socket.h>
#include "lwip/inet.h"


class Syslog
{
  private:
    ip_addr_t ip;
    int port;

    char *buf;
    int bufsize;
    int failed_sends;
    int overflows;      // How many times the buffer filled before it drained
    TaskHandle_t task;

    // Sensitive variables needing exclusive access
    int bufpos;         // Where next character will be written by charout()
    int newlinepos;     // Last position where a newline was written (or -1)
    bool overflow;      // True when log data is coming faster than we can handle

    static void syslogTask(void *arg);
    void forwardLogging();

  public:
    Syslog() : buf(0), bufsize(0), failed_sends(0), overflows(0) { rewind(); }
    // Drive-by fix, unrelated to the rest of this change: buf is new char[],
    // so plain delete is undefined behaviour.
    ~Syslog() { delete[] buf; }
    void rewind() { bufpos = 0; newlinepos = -1; overflow = false; }
    bool init(size_t buffer_size);
    void charout(int c);
    // How many datagrams the stack refused, and how many times the buffer
    // filled before the forwarding task could drain it. The log itself cannot
    // carry either without risking a loop, so both are read over REST; see
    // GET /v1/info in software/api/routes.cc.
    int failures() const { return failed_sends; }
    int overflowed() const { return overflows; }
};

extern Syslog syslog;

#endif  /* SYSLOG_H */
