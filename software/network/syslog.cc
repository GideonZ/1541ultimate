#include <sys/socket.h>

#include "syslog.h"
#include "lwip/opt.h"
#include <string.h>

// How much of the buffer one flush sends per datagram. Below the 1472-byte
// payload an untagged Ethernet frame carries, because lwIP is built with
// IP_FRAG off and would not fragment an oversized one.
static const int FLUSH_PIECE_BYTES = 1024;
#include "network_interface.h"
#include "network_config.h"


bool Syslog::open_buffer(size_t buffer_size)
{
    if (buf) {
        return true;  // Already open. The first size stands.
    }
    buf = new char[buffer_size];
    if (!buf) {
        return false;  // charout writes nothing while bufsize stays 0.
    }
    bufsize = (int)buffer_size;
    // Clears `overflow` as well: a character written before the buffer
    // existed sets it, and charout returns for the rest of the run while it
    // is set.
    rewind();
    return true;
}

void Syslog::close_buffer()
{
    // For a device with no syslog server configured, which is the default.
    // 16 KB is worth reclaiming on the U2, whose heap is the tightest.
    if (!buf) {
        return;
    }
    char *old = buf;
    ENTER_SAFE_SECTION;
    buf = 0;
    bufsize = 0;
    rewind();
    LEAVE_SAFE_SECTION;
    delete[] old;
}

bool Syslog::init(size_t buffer_size)
{
    if (!networkConfig.cfg) {
        printf("** Network Config doesn't have a config?\n");
        return false;
    }
    const char *server = networkConfig.cfg->get_string(CFG_NETWORK_REMOTE_SYSLOG_SERVER);
    if (!server || *server == 0) {
        return false;  // Logging to syslog is disabled
    }

    // Determine ip and port
    const char *sep = strchr(server, ':');
    if (!sep) {
        port = 514;  // Default syslog port
        if (!ipaddr_aton(server, &ip)) {
            ip.addr = INADDR_ANY;
        }
    }
    else {
        int len = sep - server;
        if (len >= 7 && len <= 15) {
            char addr[16];
            memcpy(addr, server, len);
            addr[len] = 0;
            if (ipaddr_aton(addr, &ip)) {
                port = atoi(sep + 1);
                if (!port || port < 0 || port > 0xffff) {
                    ip.addr = INADDR_ANY;
                }
            }
            else {
                ip.addr = INADDR_ANY;
            }
        }
    }

    // If a valid ip is configured we enable the syslog
    if (ip.addr != INADDR_ANY) {
        // Keeps whatever open_buffer already collected, so the lines printed
        // before this point are forwarded rather than dropped.
        open_buffer(buffer_size);
        xTaskCreate(syslogTask, "Syslog Task", configMINIMAL_STACK_SIZE, this, PRIO_NETSERVICE, &task);
        printf("Sending logs to syslog server '%s'\n", server);
        return true;
    }

    // Invalid config
    printf("Invalid syslog server specified (expected <ip>[:<port>]): '%s'\n", server);
    return false;
}

void Syslog::charout(int c)
{
    if (overflow) {
        return;
    }
    if (c == '\r') {
        return;
    }
    if (bufpos < bufsize) {
        buf[bufpos] = (char)c;
        ENTER_SAFE_SECTION;
        if (c == '\n') {
            newlinepos = bufpos;
        }
        ++bufpos;
        LEAVE_SAFE_SECTION;
    }
    else {
        if (!overflow) {
            ++overflows;
        }
        overflow = true;
    }
}

void Syslog::syslogTask(void *arg)
{
    Syslog *obj = (Syslog *)arg;
    obj->forwardLogging();
    // Never reached
    for (;;) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    vTaskSuspend(NULL);
}

void Syslog::forwardLogging()
{
    // Wait until we have link on at least one of our interfaces
    while (true) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if (NetworkInterface::DoWeHaveLink()) {
            break;
        }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Wait one extra second to allow things to settle

    // Open socket for sending packets to the remote syslog server
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = ip.addr;
    sa.sin_port = htons(port);
    // Assigned to the member only once the socket is connected, so a flush
    // from another task cannot send on a half-open one.
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
    {
        if (fd >= 0) {
            closesocket(fd);
            puts("Failed to open socket for sending syslog packets, terminating syslog task\n");
        }
        else {
            puts("Failed to prepare connection to syslog server, terminating syslog task\n");
        }
        vTaskDelete(NULL);
        // Never reached
    }
    sockfd = fd;

    // Forward lines to syslog as they come in. linestartpos is a member, so
    // a flush from another task can rewind the buffer and this cursor
    // together; see Syslog::flush.
    linestartpos = 0;
    while (true) {
        // If there is at least one newline found we loop forever until a "break" is reached
        while (newlinepos >= 0) {
            // Both under one section, and the length checked before it is
            // used: a flush from another task can rewind the buffer between
            // the two reads, and memchr takes a size_t, so a negative count
            // would become four billion and read past the end of the buffer.
            ENTER_SAFE_SECTION;
            int safe_newlinepos = newlinepos;
            int safe_startpos = linestartpos;
            LEAVE_SAFE_SECTION;
            int span = safe_newlinepos - safe_startpos + 1;
            if (span <= 0) {
                break;
            }
            char *line = &buf[safe_startpos];
            char *newline = (char *)memchr(line, '\n', span);
            if (newline) {
                int linelen = newline - line;  // Excluding newline char
                if (linelen) {
                    if (send(sockfd, line, linelen, 0) < 0) {
                        // Error sending, but not really much we can do except register the fact.
                        // Can't risk logging as that could cause an infinite loop.
                        ++failed_sends;
                    }
                    vTaskDelay(5 / portTICK_PERIOD_MS);  // Throttle to 200 messages per second
                }
                linestartpos = safe_startpos + linelen + 1;

                // See if we are all caught up and can rewind the buffer
                if (linestartpos >= bufpos) {  // Preliminary quick peek
                    ENTER_SAFE_SECTION;
                    if (linestartpos >= bufpos) {
                        // We are caught up. rewind() puts linestartpos back
                        // with the rest of the cursor.
                        rewind();
                    }
                    LEAVE_SAFE_SECTION;
                    break;  // No more data, done
                }
            }
            else {
                break;  // No more newlines found in the remaining buffer, done
            }
        }

        // Check for overflow
        if (overflow) {
            ENTER_SAFE_SECTION;
            rewind();
            LEAVE_SAFE_SECTION;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);  // Wait for more data
    }
    // Never reached
}

void Syslog::flush()
{
    // For a caller that is about to stop the machine. vAssertCalled prints
    // the assertion and the task list and then spins with interrupts
    // disabled, so the forwarding task never runs again and the one message
    // worth having is the one that never leaves.
    //
    // Called from the failing task before it disables interrupts, so this is
    // an ordinary send from an ordinary context. It sends what is in the
    // buffer as one datagram rather than line by line: there is no time left
    // to throttle, and the receiver writes a datagram as it arrives.
    // Never from the stack's own thread. This is a socket call, which posts
    // to the tcpip thread's mailbox and waits for it: called from that thread
    // it would wait for itself, and the caller would block forever instead of
    // halting. An assertion inside lwIP is exactly the case this runs into.
    const char *self = pcTaskGetName(NULL);
    if (self && strcmp(self, TCPIP_THREAD_NAME) == 0) {
        return;
    }
    if (sockfd < 0 || !buf || overflow) {
        // An overflowed buffer no longer holds what the caller printed: every
        // character since it filled was dropped by charout, so sending it
        // would send the wrong bytes at the one moment they matter.
        return;
    }
    ENTER_SAFE_SECTION;
    int start = linestartpos;
    int length = bufpos - start;
    LEAVE_SAFE_SECTION;
    if (length <= 0) {
        return;
    }
    // From where the forwarding task had reached, so the log is not repeated
    // from the last rewind. That task advances the cursor after its send, so
    // one line can still arrive twice when the flush lands inside that
    // window, which is a better trade than a bounded flush that misses the
    // line it exists to deliver.
    //
    // The pieces fit one datagram: the stack is built with IP_FRAG off, so an
    // oversized send is not fragmented, and the collector reads 2048 bytes
    // per datagram. The task list this carries is several kilobytes.
    int sent = 0;
    while (sent < length) {
        int piece = length - sent;
        if (piece > FLUSH_PIECE_BYTES) {
            piece = FLUSH_PIECE_BYTES;
        }
        if (send(sockfd, &buf[start + sent], piece, 0) < 0) {
            ++failed_sends;
            return;
        }
        sent += piece;
    }
    ENTER_SAFE_SECTION;
    rewind();
    LEAVE_SAFE_SECTION;
}
