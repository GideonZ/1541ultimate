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
    TaskHandle_t task;

    // Sensitive variables needing exclusive access
    int bufpos;         // Where next character will be written by charout()
    int newlinepos;     // Last position where a newline was written (or -1)
    bool overflow;      // True when log data is coming faster than we can handle

    static void syslogTask(void *arg);
    void forwardLogging();

  public:
    Syslog() : buf(0), bufsize(0), failed_sends(0) { rewind(); }
    ~Syslog() { if (buf) delete buf; }
    void rewind() { bufpos = 0; newlinepos = -1; overflow = false; }
    bool init(size_t buffer_size);
    void charout(int c);
};

extern Syslog syslog;

#endif  /* SYSLOG_H */
