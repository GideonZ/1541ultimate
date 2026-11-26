#ifndef SYSLOG_H
#define SYSLOG_H

#include "FreeRTOS.h"
#include "task.h"

#include <sys/socket.h>
#include "lwip/inet.h"
#include "config.h"


class Syslog : public ConfigurableObject
{
  private:
    ip_addr_t ip;
    int port;

    char *buf;
    int bufsize;
    int failed_sends;
    TaskHandle_t task;

    // Sensitive variables needing exclusive access
    volatile bool enabled;  // A valid server is configured; ip and port are only valid then
    volatile unsigned generation;  // Counts changes of enabled/ip/port, so the task can notice them
    int bufpos;         // Where next character will be written by charout()
    int newlinepos;     // Last position where a newline was written (or -1)
    bool overflow;      // True when log data is coming faster than we can handle

    volatile bool applying;  // effectuate_settings() is running in some task
    volatile bool reapply;   // ...and another change arrived meanwhile
    void applySetting(void);

    static void syslogTask(void *arg);
    void forwardLogging();

  public:
    Syslog() : port(0), buf(0), bufsize(0), failed_sends(0), task(0), enabled(false), generation(0), applying(false), reapply(false) { ip.addr = 0; rewind(); }
    ~Syslog() { if (buf) delete buf; }
    void rewind() { bufpos = 0; newlinepos = -1; overflow = false; }
    static bool parseServer(const char *server, ip_addr_t *ip, int *port);
    bool init(size_t buffer_size);
    void effectuate_settings(void);
    void charout(int c);
};

extern Syslog syslog;

#endif  /* SYSLOG_H */
