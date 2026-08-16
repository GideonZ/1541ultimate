#include <sys/socket.h>

#include "syslog.h"
#include "network_interface.h"
#include "network_config.h"


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
        bufsize = buffer_size;
        buf = new char[bufsize];
        bufpos = 0;
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
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0 || connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
    {
        if (sockfd >= 0) {
            closesocket(sockfd);
            puts("Failed to open socket for sending syslog packets, terminating syslog task\n");
        }
        else {
            puts("Failed to prepare connection to syslog server, terminating syslog task\n");
        }
        vTaskDelete(NULL);
        // Never reached
    }

    // Forward lines to syslog as they come in
    int linestartpos = 0;  // Where the next line to be sent starts
    while (true) {
        // If there is at least one newline found we loop forever until a "break" is reached
        while (newlinepos >= 0) {
            ENTER_SAFE_SECTION;
            int safe_newlinepos = newlinepos;
            LEAVE_SAFE_SECTION;
            char *line = &buf[linestartpos];
            char *newline = (char *)memchr(line, '\n', safe_newlinepos - linestartpos + 1);
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
                linestartpos += linelen + 1;

                // See if we are all caught up and can rewind the buffer
                if (linestartpos >= bufpos) {  // Preliminary quick peek
                    ENTER_SAFE_SECTION;
                    if (linestartpos >= bufpos) {
                        // We are caught up
                        rewind();
                        linestartpos = 0;
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
            linestartpos = 0;
            LEAVE_SAFE_SECTION;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);  // Wait for more data
    }
    // Never reached
}
