#include "ftp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "lwip/sockets.h"
#include "netdb.h"

static void set_socket_recv_timeout(int fd, int seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
}

FTPClient::FTPClient()
    : ctrl_fd(-1)
    , response_code(0)
{
    response[0] = '\0';
}

FTPClient::~FTPClient()
{
    disconnect();
}

int FTPClient::read_response()
{
    // Read lines until we get a final response (code + space, not code + dash)
    int pos = 0;
    response_code = 0;
    response[0] = '\0';

    while (pos < FTP_MAX_RESPONSE - 1) {
        char c;
        int n = recv(ctrl_fd, &c, 1, 0);
        if (n <= 0) {
            printf("[FTP] recv error in read_response\n");
            return -1;
        }
        response[pos++] = c;
        response[pos] = '\0';

        // Check for complete final line: "NNN " at start of line + '\n'
        if (c == '\n' && pos >= 4) {
            // Find the start of the last line
            int line_start = pos - 1;
            while (line_start > 0 && response[line_start - 1] != '\n') {
                line_start--;
            }
            // Final response line has "NNN " (digit digit digit space)
            if ((pos - line_start) >= 4 &&
                response[line_start] >= '1' && response[line_start] <= '5' &&
                response[line_start + 1] >= '0' && response[line_start + 1] <= '9' &&
                response[line_start + 2] >= '0' && response[line_start + 2] <= '9' &&
                response[line_start + 3] == ' ') {
                response_code = (response[line_start] - '0') * 100 +
                                (response[line_start + 1] - '0') * 10 +
                                (response[line_start + 2] - '0');
                return response_code;
            }
        }
    }
    printf("[FTP] response buffer overflow\n");
    return -1;
}

int FTPClient::send_cmd(const char *cmd)
{
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    if (len < 0 || len >= (int)sizeof(buf)) {
        return -1;
    }
    if (send_all(ctrl_fd, buf, len) < 0) {
        printf("[FTP] send failed for '%s'\n", cmd);
        return -1;
    }
    return read_response();
}

int FTPClient::send_cmd(const char *cmd, const char *arg)
{
    char buf[320];
    int len = snprintf(buf, sizeof(buf), "%s %s\r\n", cmd, arg);
    if (len < 0 || len >= (int)sizeof(buf)) {
        return -1;
    }
    if (send_all(ctrl_fd, buf, len) < 0) {
        printf("[FTP] send failed for '%s %s'\n", cmd, arg);
        return -1;
    }
    return read_response();
}

int FTPClient::send_all(int fd, const void *buf, int len)
{
    const uint8_t *p = (const uint8_t *)buf;
    int done = 0;

    while (done < len) {
        int n = send(fd, p + done, len - done, 0);
        if (n <= 0) {
            return -1;
        }
        done += n;
    }
    return 0;
}

int FTPClient::parse_pasv(uint32_t &ip, uint16_t &port)
{
    // Find '(' in PASV response: "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)"
    char *p = strchr(response, '(');
    if (!p) {
        return -1;
    }
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(p + 1, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        return -1;
    }
    ip = ((uint32_t)h1) | ((uint32_t)h2 << 8) | ((uint32_t)h3 << 16) | ((uint32_t)h4 << 24);
    port = (uint16_t)((p1 << 8) | p2);
    return 0;
}

int FTPClient::open_data_connection()
{
    int code = send_cmd("PASV");
    if (code != 227) {
        printf("[FTP] PASV failed: %d\n", code);
        return -1;
    }

    uint32_t ip;
    uint16_t port;
    if (parse_pasv(ip, port) < 0) {
        printf("[FTP] Failed to parse PASV response\n");
        return -1;
    }

    int data_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (data_fd < 0) {
        printf("[FTP] data socket() failed\n");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip; // already in network byte order

    if (lwip_connect(data_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[FTP] data connect failed to port %d\n", port);
        lwip_close(data_fd);
        return -1;
    }
    return data_fd;
}

int FTPClient::open(const char *host, uint16_t port)
{
    disconnect();

    struct hostent my_host, *ret_host;
    char hostbuf[256];
    int error;

    if (lwip_gethostbyname_r(host, &my_host, hostbuf, sizeof(hostbuf), &ret_host, &error) != 0) {
        printf("[FTP] DNS lookup failed for '%s'\n", host);
        return -1;
    }

    ctrl_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctrl_fd < 0) {
        printf("[FTP] socket() failed\n");
        return -1;
    }
    set_socket_recv_timeout(ctrl_fd, 10);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr, ret_host->h_addr, sizeof(serv_addr.sin_addr));

    printf("[FTP] Connecting to %s:%d...\n", host, port);
    if (lwip_connect(ctrl_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("[FTP] connect() failed\n");
        lwip_close(ctrl_fd);
        ctrl_fd = -1;
        return -1;
    }

    // Read server welcome
    int code = read_response();
    if (code < 200 || code >= 300) {
        printf("[FTP] Unexpected welcome: %d\n", code);
        disconnect();
        return -1;
    }

    printf("[FTP] Connected: %s\n", response);
    return 0;
}

int FTPClient::login(const char *user, const char *pass)
{
    int code = send_cmd("USER", user);
    if (code == 230) {
        return 0; // logged in without password
    }
    if (code != 331) {
        printf("[FTP] USER failed: %d\n", code);
        return -1;
    }
    code = send_cmd("PASS", pass);
    if (code != 230) {
        printf("[FTP] PASS failed: %d\n", code);
        return -1;
    }
    return 0;
}

void FTPClient::disconnect()
{
    if (ctrl_fd >= 0) {
        send_cmd("QUIT");
        lwip_close(ctrl_fd);
        ctrl_fd = -1;
    }
    response_code = 0;
}

bool FTPClient::is_connected()
{
    if (ctrl_fd < 0) {
        return false;
    }
    return (pwd(NULL, 0) == 0);
}

int FTPClient::cwd(const char *path)
{
    int code = send_cmd("CWD", path);
    return (code == 250) ? 0 : -1;
}

int FTPClient::pwd(char *buf, int bufsize)
{
    int code = send_cmd("PWD");
    if (code != 257) {
        return -1;
    }
    if (!bufsize) {
        return 0; // don't look at the path itself
    }
    // Parse "257 "/path" ..."
    char *start = strchr(response, '"');
    if (!start) {
        return -1;
    }
    start++;
    char *end = strchr(start, '"');
    if (!end) {
        return -1;
    }
    int len = end - start;
    if (len >= bufsize) {
        len = bufsize - 1;
    }
    memcpy(buf, start, len);
    buf[len] = '\0';
    return 0;
}

int FTPClient::type_binary()
{
    int code = send_cmd("TYPE I");
    return (code == 200) ? 0 : -1;
}

int FTPClient::list(const char *path, char *buf, int bufsize, int *bytes_read)
{
    *bytes_read = 0;
    char old_dir[256];
    bool restore_dir = (pwd(old_dir, sizeof(old_dir)) == 0);

    // CWD to target directory first.  Passing paths with spaces as a
    // LIST argument breaks on many FTP servers because they parse the
    // argument like shell words.  CWD doesn't have this problem.
    if (path && *path) {
        if (send_cmd("CWD", path) != 250) {
            printf("[FTP] CWD '%s' failed\n", path);
            return -1;
        }
    }

    int data_fd = open_data_connection();
    if (data_fd < 0) {
        if (restore_dir) {
            cwd(old_dir);
        }
        return -1;
    }

    int code = send_cmd("LIST");
    if (code != 150 && code != 125) {
        lwip_close(data_fd);
        if (restore_dir) {
            cwd(old_dir);
        }
        return -1;
    }

    // Set receive timeout so we don't block forever if the server is
    // slow to close the data connection after sending the listing.
    set_socket_recv_timeout(data_fd, 3);

    // Read directory listing from data connection
    int total = 0;
    int idle_rounds = 0;
    while (total < bufsize - 1) {
        int n = recv(data_fd, buf + total, bufsize - 1 - total, 0);
        if (n > 0) {
            total += n;
            idle_rounds = 0;
        } else if (n == 0) {
            break;  // server closed connection — transfer complete
        } else {
            // recv returned -1: timeout or error
            idle_rounds++;
            if (idle_rounds >= 2) {
                // Two consecutive timeouts with no new data — done
                break;
            }
            // First timeout: try once more in case data is still arriving
        }
    }
    buf[total] = '\0';
    *bytes_read = total;

    lwip_close(data_fd);

    // Read transfer complete response
    code = read_response();
    if (code != 226 && code != 250) {
        printf("[FTP] LIST transfer not completed cleanly: %d\n", code);
        if (restore_dir) {
            cwd(old_dir);
        }
        return -1;
    }
    if (restore_dir) {
        cwd(old_dir);
    }
    return 0;
}

int FTPClient::retr(const char *remote_path, File *file, int *bytes_read)
{
    *bytes_read = 0;

    int data_fd = open_data_connection();
    if (data_fd < 0) {
        return -1;
    }

    int code = send_cmd("RETR", remote_path);
    if (code != 150 && code != 125) {
        lwip_close(data_fd);
        return -1;
    }

    // Set receive timeout for data connection
    set_socket_recv_timeout(data_fd, 5);

    int total = 0;
    int idle = 0;
    int ret = 0;

    uint8_t *buf = new uint8_t[4096];
    if (!buf) {
        lwip_close(data_fd);
        return -1;
    }
    uint32_t tr;
    FRESULT fres;
    while (1) {
        int n = recv(data_fd, buf, 4096, 0);
        if (n > 0) {
            total += n;
            fres = file->write(buf, n, &tr);
            if (fres != FR_OK || tr != (uint32_t)n) {
                ret = -1;
                break;
            }
            idle = 0;
        } else if (n == 0) {
            break;
        } else {
            idle++;
            if (idle >= 2) {
                ret = -1;
                break;
            }
        }
    }
    delete[] buf;
    *bytes_read = total;

    lwip_close(data_fd);

    code = read_response();
    if (code != 226 && code != 250) {
        printf("[FTP] RETR transfer not completed cleanly: %d\n", code);
        return -1;
    }
    return ret;
}

int FTPClient::stor(const char *remote_path, File *file)
{
    FRESULT fres = file->seek(0); // reset to begin
    if (fres != FR_OK) {
        return -1;
    }

    int data_fd = open_data_connection();
    if (data_fd < 0) {
        return -1;
    }

    int code = send_cmd("STOR", remote_path);
    if (code != 150 && code != 125) {
        lwip_close(data_fd);
        return -1;
    }

    int sent = 0;
    uint8_t *buf = new uint8_t[4096];
    if (!buf) {
        lwip_close(data_fd);
        return -1;
    }

    while (1) {
        uint32_t tr = 0;
        fres = file->read(buf, 4096, &tr);
        if (fres != FR_OK) {
            break;
        }

        if (tr) {
            if (send_all(data_fd, buf, tr) < 0) {
                printf("[FTP] STOR send error after %d bytes\n", sent);
                delete[] buf;
                lwip_close(data_fd);
                return -1;
            }
            sent += tr;
        } else {
            break;
        }
    }

    delete[] buf;
    lwip_close(data_fd);

    code = read_response();
    if (code != 226 && code != 250) {
        printf("[FTP] STOR transfer not completed cleanly: %d\n", code);
        return -1;
    }
    return 0;
}

int FTPClient::dele(const char *path)
{
    int code = send_cmd("DELE", path);
    return (code == 250) ? 0 : -1;
}

int FTPClient::mkd(const char *path)
{
    int code = send_cmd("MKD", path);
    return (code == 257) ? 0 : -1;
}

int FTPClient::rmd(const char *path)
{
    int code = send_cmd("RMD", path);
    return (code == 250) ? 0 : -1;
}

int FTPClient::rnfr_rnto(const char *from, const char *to)
{
    int code = send_cmd("RNFR", from);
    if (code != 350) {
        return -1;
    }
    code = send_cmd("RNTO", to);
    return (code == 250) ? 0 : -1;
}
