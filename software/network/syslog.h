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
    int sockfd;         // -1 until the forwarding task has opened it
    TaskHandle_t task;

    // Sensitive variables needing exclusive access
    int bufpos;         // Where next character will be written by charout()
    int newlinepos;     // Last position where a newline was written (or -1)
    int linestartpos;   // Where the next line to be sent starts. A member
                        // rather than a local of forwardLogging, because
                        // flush() rewinds the buffer from another task and
                        // a cursor it could not reach would then point past
                        // the end of the data.
    bool overflow;      // True when log data is coming faster than we can handle

    static void syslogTask(void *arg);
    void forwardLogging();

  public:
    Syslog() : buf(0), bufsize(0), failed_sends(0), overflows(0), sockfd(-1)
        { rewind(); }
    ~Syslog() { if (buf) delete buf; }
    void rewind() { bufpos = 0; newlinepos = -1; linestartpos = 0; overflow = false; }
    // Allocate the buffer before there is anywhere to send it. Everything
    // printed before the network configuration exists is dropped otherwise,
    // which is the whole boot up to the init functions: the product version
    // banner, the FPGA capabilities and every init function's own output.
    bool open_buffer(size_t buffer_size);
    // Give the buffer back, for a device with nowhere to send it.
    void close_buffer();
    bool init(size_t buffer_size);
    void charout(int c);
    // How many datagrams the stack refused, and how many times the buffer
    // filled before the forwarding task could drain it. The log itself cannot
    // carry either without risking a loop, so both are read over REST.
    int failures() const { return failed_sends; }
    int overflowed() const { return overflows; }
    // Send whatever the forwarding task has not sent yet, from the calling
    // task, for a caller that is about to stop the machine and cannot wait
    // for that task to run again. Sent in datagram-sized pieces, because the
    // stack is built without IP fragmentation.
    void flush();
};

extern Syslog syslog;

#endif  /* SYSLOG_H */
