#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#include <stdint.h>
#include "filemanager.h"

#define FTP_MAX_RESPONSE 512

class FTPClient
{
    int ctrl_fd;
    char response[FTP_MAX_RESPONSE];
    int response_code;

    int read_response();
    int send_cmd(const char *cmd);
    int send_cmd(const char *cmd, const char *arg);
    int open_data_connection();
    int parse_pasv(uint32_t &ip, uint16_t &port);

public:
    FTPClient();
    ~FTPClient();

    int open(const char *host, uint16_t port);
    int login(const char *user, const char *pass);
    void disconnect();
    bool is_connected() { return ctrl_fd >= 0; }

    int cwd(const char *path);
    int pwd(char *buf, int bufsize);
    int list(const char *path, char *buf, int bufsize, int *bytes_read);
    int retr(const char *remote_path, File *file, int *bytes_read);
    int stor(const char *remote_path, File *file);
    int dele(const char *path);
    int mkd(const char *path);
    int rmd(const char *path);
    int rnfr_rnto(const char *from, const char *to);
    int type_binary();

    const char *last_response() { return response; }
    int last_code() { return response_code; }
};

#endif
