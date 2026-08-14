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
    int sockfd;         // -1 until the forwarding task has opened it
    TaskHandle_t task;

    // Sensitive variables needing exclusive access
    int bufpos;         // Where next character will be written by charout()
    int newlinepos;     // Last position where a newline was written (or -1)
    bool overflow;      // True when log data is coming faster than we can handle

    static void syslogTask(void *arg);
    void forwardLogging();

  public:
    Syslog() : buf(0), bufsize(0), failed_sends(0), sockfd(-1) { rewind(); }
    ~Syslog() { if (buf) delete buf; }
    void rewind() { bufpos = 0; newlinepos = -1; overflow = false; }
    // Allocate the buffer before there is anywhere to send it. Everything
    // printed before the network configuration exists is dropped otherwise,
    // which is the whole boot up to the init functions: the product version
    // banner, the FPGA capabilities and every init function's own output.
    bool open_buffer(size_t buffer_size);
    bool init(size_t buffer_size);
    void charout(int c);
    // How many datagrams the stack refused. The log itself cannot carry a
    // send failure without risking a loop, so the count is read over REST.
    int failures() const { return failed_sends; }
    // Send whatever is in the buffer now, from the calling task. For a
    // caller that is about to stop the machine and cannot wait for the
    // forwarding task to run again.
    void flush();
};

extern Syslog syslog;

#endif  /* SYSLOG_H */
