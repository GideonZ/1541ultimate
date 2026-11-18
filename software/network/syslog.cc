#include <sys/socket.h>

#include "syslog.h"
#include "network_interface.h"
#include "network_config.h"


// Parses "<ip>[:<port>]". An empty string is not an error, it just means "off".
bool Syslog::parseServer(const char *server, ip_addr_t *ip, int *port)
{
    ip->addr = INADDR_ANY;
    *port = 0;
    if (!server || *server == 0) {
        return false;
    }
    const char *sep = strchr(server, ':');
    if (!sep) {
        *port = 514;  // Default syslog port
        if (!ipaddr_aton(server, ip)) {
            ip->addr = INADDR_ANY;
        }
    }
    else {
        int len = sep - server;
        if (len >= 7 && len <= 15) {
            char addr[16];
            memcpy(addr, server, len);
            addr[len] = 0;
            if (ipaddr_aton(addr, ip)) {
                *port = atoi(sep + 1);
                if (!*port || *port < 0 || *port > 0xffff) {
                    ip->addr = INADDR_ANY;
                }
            }
            else {
                ip->addr = INADDR_ANY;
            }
        }
    }
    return ip->addr != INADDR_ANY;
}

bool Syslog::init(size_t buffer_size)
{
    if (!networkConfig.cfg) {
        printf("** Network Config doesn't have a config?\n");
        return false;
    }
    bufsize = buffer_size;
    // Follow later changes of the setting, without a restart.
    cfg = networkConfig.cfg;
    cfg->addObject(this);
    effectuate_settings();
    return enabled;
}

// Called by the config store (from whichever task changed the setting) when the
// network settings change. Only publishes the wanted state; the syslog task opens
// and closes the socket itself, as lwIP sockets must not be closed from another task.
void Syslog::effectuate_settings(void)
{
    const char *server = networkConfig.cfg->get_string(CFG_NETWORK_REMOTE_SYSLOG_SERVER);
    ip_addr_t new_ip;
    int new_port;
    bool valid = parseServer(server, &new_ip, &new_port);
    bool invalid = !valid && server && *server;

    if (valid && !buf) {
        // Allocated on first use and then kept: charout() may be running in any task,
        // so the buffer can never be freed safely. Devices that never enable syslog
        // do not pay for it.
        buf = new char[bufsize];
        if (!buf) {
            printf("No memory for the syslog buffer\n");
            valid = false;
        }
    }
    if (valid && !task) {
        rewind();
        if (xTaskCreate(syslogTask, "Syslog Task", configMINIMAL_STACK_SIZE, this, PRIO_NETSERVICE, &task) != pdPASS) {
            task = NULL;
            printf("Failed to start the syslog task\n");
            valid = false;
        }
    }

    bool changed;
    ENTER_SAFE_SECTION;
    changed = (valid != enabled) || (valid && (new_ip.addr != ip.addr || new_port != port));
    if (changed) {
        enabled = valid;
        if (valid) {
            ip = new_ip;
            port = new_port;
        }
        ++generation;
    }
    LEAVE_SAFE_SECTION;

    if (!changed) {
        return;
    }
    if (valid) {
        printf("Sending logs to syslog server '%s'\n", server);
    }
    else if (invalid) {
        printf("Invalid syslog server specified (expected <ip>[:<port>]): '%s'\n", server);
    }
    else {
        printf("Syslog server cleared, no longer sending logs\n");
    }
}

void Syslog::charout(int c)
{
    if (!enabled || overflow) {
        return;
    }
    if (c == '\r') {
        return;
    }
    // Tasks print concurrently, so the position is tested and used inside one safe section;
    // tested outside it, another writer could move it past the end between test and store.
    ENTER_SAFE_SECTION;
    if (!enabled) {
        // Switched off since the quick test above
    }
    else if (bufpos < bufsize) {
        buf[bufpos] = (char)c;
        if (c == '\n') {
            newlinepos = bufpos;
        }
        ++bufpos;
    }
    else {
        overflow = true;
    }
    LEAVE_SAFE_SECTION;
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

    int sockfd = -1;
    unsigned active_generation = 0;  // The state the socket below was made for
    int linestartpos = 0;  // Where the next line to be sent starts
    while (true) {
        // Follow changes of the setting: close, and if still wanted reopen, our socket.
        ENTER_SAFE_SECTION;
        unsigned wanted_generation = generation;
        bool wanted = enabled;
        ip_addr_t wanted_ip = ip;
        int wanted_port = port;
        LEAVE_SAFE_SECTION;
        if (wanted_generation != active_generation) {
            active_generation = wanted_generation;
            if (sockfd >= 0) {
                closesocket(sockfd);
                sockfd = -1;
            }
            if (wanted) {
                struct sockaddr_in sa;
                memset(&sa, 0, sizeof(sa));
                sa.sin_family = AF_INET;
                sa.sin_addr.s_addr = wanted_ip.addr;
                sa.sin_port = htons(wanted_port);
                sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                if (sockfd < 0 || connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
                    if (sockfd >= 0) {
                        closesocket(sockfd);
                        sockfd = -1;
                        puts("Failed to open socket for sending syslog packets\n");
                    }
                    else {
                        puts("Failed to prepare connection to syslog server\n");
                    }
                }
            }
            else {
                // Switched off: charout() no longer adds anything, drop what is pending
                ENTER_SAFE_SECTION;
                rewind();
                LEAVE_SAFE_SECTION;
                linestartpos = 0;
            }
        }
        if (sockfd < 0) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

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

                if (generation != active_generation) {
                    break;  // The setting changed, deal with it before sending more
                }

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
