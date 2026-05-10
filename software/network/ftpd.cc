#include "ftpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "pattern.h"

#include <ctype.h>
#include <time.h>

#include "vfs.h"
#include "rtc.h"
#include "network_config.h"
#include "product.h"

static int num_threads = 0;

#ifdef FTPD_DEBUG
int dbg_printf(const char *fmt, ...);
#else
#ifdef _MSC_VER
#define dbg_printf(x) /* x */
#else
#define dbg_printf(f, ...) /* */
#endif
#endif

#define msg110 "110 MARK %s = %s."
/*
 110 Restart marker reply.
 In this case, the text is exact and not left to the
 particular implementation; it must read:
 MARK yyyy = mmmm
 Where yyyy is User-process data stream marker, and mmmm
 server's equivalent marker (note the spaces between markers
 and "=").
 */
#define msg120 "120 Service ready in nnn minutes."
#define msg125 "125 Data connection already open; transfer starting."
#define msg150 "150 File status okay; about to open data connection."
#define msg150recv "150 Opening BINARY mode data connection for %s (%d bytes)."
#define msg150stor "150 Opening BINARY mode data connection for %s."
#define msg200 "200 Command okay."
#define msg202 "202 Command not implemented, superfluous at this site."
#define msg211 "211-Features:\r\n MLST size*;type*;modify*\r\n MLSD\r\n211 End"
#define msg212 "212 Directory status."
#define msg213 "213 %d"
#define msg214 "214 %s."
/*
 214 Help message.
 On how to use the server or the meaning of a particular
 non-standard command.  This reply is useful only to the
 human user.
 */
#define msg214SYST "214 %s system type."
/*
 215 NAME system type.
 Where NAME is an official system name from the list in the
 Assigned Numbers document.
 */
#define msg220 "220 1541Ultimate-II FTP Server ready."
/*
 220 Service ready for new user.
 */
#define msg221 "221 Goodbye."
/*
 221 Service closing control connection.
 Logged out if appropriate.
 */
#define msg225 "225 Data connection open; no transfer in progress."
#define msg226 "226 Closing data connection."
/*
 Requested file action successful (for example, file
 transfer or file abort).
 */
#define msg227 "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)."
/*
 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2).
 */
#define msg230 "230 User logged in, proceed."
#define msg250 "250 Requested file action okay, completed."
#define msg257PWD "257 \"%s\" is current directory."
#define msg257MKD "257 \"%s/%s\" created."
/*
 257 "PATHNAME" created.
 */
#define msg331 "331 User name okay, need password."
#define msg332 "332 Need account for login."
#define msg350 "350 Requested file action pending further information."
#define msg421 "421 Service not available, closing control connection."
/*
 This may be a reply to any command if the service knows it
 must shut down.
 */
#define msg425 "425 Can't open data connection, use PORT or PASV first."
#define msg426 "426 Connection closed; transfer aborted."
#define msg450 "450 Requested file action not taken."
/*
 File unavailable (e.g., file busy).
 */
#define msg451 "451 Requested action aborted: local error in processing."
#define msg452 "452 Requested action not taken."
/*
 Insufficient storage space in system.
 */
#define msg500 "500 Syntax error, command unrecognized."
/*
 This may include errors such as command line too long.
 */
#define msg501 "501 Syntax error in parameters or arguments."
#define msg502 "502 Command not implemented."
#define msg503 "503 Bad sequence of commands."
#define msg504 "504 Command not implemented for that parameter."
#define msg530 "530 Not logged in."
#define msg532 "532 Need account for storing files."
#define msg550 "550 Requested action not taken."
/*
 File unavailable (e.g., file not found, no access).
 */
#define msg551 "551 Requested action aborted: page type unknown."
#define msg552 "552 Requested file action aborted."
/*
 Exceeded storage allocation (for current directory or
 dataset).
 */
#define msg553 "553 Requested action not taken."
/*
 File name not allowed.
 */

static const char *month_table[16] = { "Nul", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
        "M13", "M14", "M15" };

static void ftp_set_socket_timeout(int socket_fd, long seconds, long usec)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = usec;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
}

static void ftp_set_control_socket_timeouts(int socket_fd)
{
    struct timeval recv_tv;
    recv_tv.tv_sec = 0;
    recv_tv.tv_usec = 100000; // Keep the historical control-socket poll interval.
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&recv_tv, sizeof(recv_tv));

    struct timeval send_tv;
    send_tv.tv_sec = 5;
    send_tv.tv_usec = 0;
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&send_tv, sizeof(send_tv));
}

static FTPTransferResult send_all(int socket, const char *buffer, int length)
{
    while (length > 0) {
        int sent = send(socket, buffer, length, 0);
        if (sent <= 0) {
            return FTP_TRANSFER_ABORTED;
        }
        buffer += sent;
        length -= sent;
    }
    return FTP_TRANSFER_OK;
}

static const char *transfer_result_message(FTPTransferResult result)
{
    switch (result) {
    case FTP_TRANSFER_OK:
        return msg226;
    case FTP_TRANSFER_SETUP_FAILED:
        return msg425;
    case FTP_TRANSFER_ABORTED:
        return msg426;
    case FTP_TRANSFER_STORAGE_ERROR:
        return msg452;
    default:
        return msg226;
    }
}

static int EndsWith(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}
FTPDaemon *ftpd = NULL; 

FTPDaemon::FTPDaemon()
{
    xTaskCreate(ftp_listen_task, "FTP Listener", configMINIMAL_STACK_SIZE, this, PRIO_NETSERVICE, &listenTaskHandle);
}

void FTPDaemon::ftp_listen_task(void *a)
{
    FTPDaemon *daemon = (FTPDaemon *) a;
    int error = daemon->listen_task();
    printf("FTPDaemon listen task exiting. Error = %d\n", error);
    vTaskDelete(NULL);
}

int FTPDaemon::listen_task()
{
    while (networkConfig.cfg->get_value(CFG_NETWORK_FTP_SERVICE) == 0) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    puts("FTP server starting");

    int sockfd, portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        puts("FTPD: ERROR opening socket");
        return -1;
    }
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    portno = 21;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
            sizeof(serv_addr)) < 0) {
        puts("FTPD: ERROR on binding");
        return -2;
    }

    listen(sockfd, 2);

    while (1) {
        clilen = sizeof(cli_addr);
        int actual_socket = accept(sockfd, (struct sockaddr * ) &cli_addr, &clilen);
        if (actual_socket < 0) {
            puts("FTPD: ERROR on accept");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;  // Remote probably closed, just wait for another connection
        }

        ftp_set_control_socket_timeouts(actual_socket);

        FTPDaemonThread *thread = new FTPDaemonThread(actual_socket, cli_addr.sin_addr.s_addr, cli_addr.sin_port);

        BaseType_t res = xTaskCreate(FTPDaemonThread::run, "FTP Task", configMINIMAL_STACK_SIZE, thread, PRIO_NETSERVICE, NULL);
        if (res != pdPASS) {
            puts("FTPD: xTaskCreate failed; dropping connection");
            closesocket(actual_socket);
            delete thread;
            num_threads--;
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
    }
}

static const uint16_t bind_port_first = 51000;
static const uint16_t bind_port_last = 61000;
static const int bind_port_count = bind_port_last - bind_port_first;
static uint16_t bind_port = bind_port_first;

FTPDaemonThread::FTPDaemonThread(int socket, uint32_t addr, uint16_t port) :
        data_connections(4, NULL)
{
    num_threads++;
    this->socket = socket;

    {
        uint8_t *ip = (uint8_t *)&addr;
        your_ip[0] = ip[0]; //addr >> 24;
        your_ip[1] = ip[1]; //(addr >> 16) & 0xFF;
        your_ip[2] = ip[2]; //(addr >> 8) & 0xFF;
        your_ip[3] = ip[3]; //addr & 0xFF;
    }

    your_port = port;

    printf("FTPDaemonThread(%d): connection from %d.%d.%d.%d:%d\n", num_threads, your_ip[0], your_ip[1], your_ip[2], your_ip[3], port);

    /* Initialize the structure. */
    state = FTPD_IDLE;
    vfs = 0;
    memset(&dataip, 0, sizeof(dataip));
    dataport = 0;
    passive = 0;
    connection = 0;
    renamefrom = 0;
    func = 0;
    container_mode = e_vfs_files_only;
    uint32_t fattime = rtc.get_fat_time();
    current_year = 1980 + (fattime >> 25);
}

FTPDaemonThread::~FTPDaemonThread()
{
    destroy_connection();
    if (renamefrom) {
        delete[] renamefrom;
    }
    if (vfs) {
        vfs_closefs(vfs);
    }
}

void FTPDaemonThread::destroy_connection()
{
    if (connection) {
        delete connection;
        connection = 0;
    }
}

void FTPDaemonThread::run(void *a)
{
    FTPDaemonThread *thread = (FTPDaemonThread *) a;
    thread->handle_connection();
    if (thread->socket >= 0) {
        closesocket(thread->socket);
    }
    delete thread;
    num_threads--;
    vTaskDelete(NULL);
}

uint16_t FTPDaemonThread::getBindPort()
{
    uint16_t port;

    // Multiple FTP control threads can enter PASV concurrently.
    // Keep passive-port allocation serialized so two threads do not race on the same port.
    portENTER_CRITICAL();
    if (bind_port >= bind_port_last) {
        bind_port = bind_port_first;
    }
    port = bind_port++;
    portEXIT_CRITICAL();

    return port;
}

int FTPDaemonThread::handle_connection()
{
    sockaddr my_addr;
    socklen_t size = sizeof(my_addr);
    getsockname(socket, &my_addr, &size);

    for (int i = 0; i < 4; i++)
        my_ip[i] = (uint8_t) my_addr.sa_data[2 + i];

    vfs = vfs_openfs();
    if (!vfs) {
        return -ENXIO;
    }
    //send_msg(msg220);
    send_msg("220 %s FTP: IP = %d.%d.%d.%d. Hello %d.%d.%d.%d:%d", getProductString(), my_ip[0], my_ip[1], my_ip[2], my_ip[3], your_ip[0],
            your_ip[1], your_ip[2], your_ip[3], your_port);
    authenticated = 'n';

    int idx = 0;
    while (socket >= 0) {
        int n = recv(socket, &command_buffer[idx], 1, 0);
        if (n > 0) {
            if ((command_buffer[idx] == '\n') || (command_buffer[idx] == '\r')) {
                command_buffer[idx] = 0;
                if (idx > 0) {
                    dbg_printf("FTPD: '%s'\n", command_buffer);
                    dispatch_command(command_buffer, idx);
                    idx = 0;
                }
            } else {
                idx++;
                if (idx >= COMMAND_BUFFER_SIZE)
                    idx = COMMAND_BUFFER_SIZE - 1;
            }
        } else if (n < 0) {
            if (errno == EAGAIN)
                continue;
            dbg_printf("FTPD: ERROR reading from socket %d. Errno = %d", n, errno);
            return errno;
        } else { // n == 0
            dbg_printf("FTPD: Socket got closed\n");
            break;
        }
    }
    return ERR_OK;
}

void FTPDaemonThread::send_msg(const char *msg, ...)
{
    va_list arg;
    char buffer[600];
    int len;

    va_start(arg, msg);
    vsprintf(buffer, msg, arg);
    va_end(arg);
    strcat(buffer, "\r\n");
    len = strlen(buffer);
    if ((socket >= 0) && (send_all(socket, buffer, len) != FTP_TRANSFER_OK)) {
        dbg_printf("FTPD: ERROR writing to control socket\n");
        shutdown(socket, 2);
        closesocket(socket);
        socket = -1;
    }
    dbg_printf("FTPD response: %s", buffer);
}

void FTPDaemonThread::cmd_user(const char *arg)
{
    if (strcasecmp(arg, "dirs") == 0) {
        container_mode = e_vfs_dirs_only;
    } else if (strcasecmp(arg, "into") == 0) {
        container_mode = e_vfs_dirs_only;
    } else if(strcasecmp(arg, "both") == 0) {
        container_mode = e_vfs_double_listed;
    }
    send_msg(msg331);
    state = FTPD_PASS;
}

void FTPDaemonThread::cmd_pass(const char *arg)
{
    const char *password = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
    if(!*password || strcmp(arg, password) == 0) {
	// Empty password configured or password matched, authenticated!
        authenticated = 'y';
        send_msg(msg230);
    }
    else {
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Throttle failed password attempts
        send_msg(msg530);
    }
    state = FTPD_IDLE;
}

void FTPDaemonThread::cmd_port(const char *arg)
{
    int nr;
    unsigned pHi, pLo;
    unsigned ip[4];

    nr = sscanf(arg, "%d,%d,%d,%d,%d,%d", &(ip[0]), &(ip[1]), &(ip[2]), &(ip[3]), &pHi, &pLo);
    if (nr != 6) {
        send_msg(msg501);
    } else {
        printf("Got: %d.%d.%d.%d port %d.%d\n", ip[0], ip[1], ip[2], ip[3], pHi, pLo);
        IP4_ADDR(&dataip, (uint8_t) ip[0], (uint8_t) ip[1], (uint8_t) ip[2], (uint8_t) ip[3]);
        dataport = ((uint16_t) pHi << 8) | (uint16_t) pLo;

        destroy_connection();
        connection = new FTPDataConnection(this);
        passive = 0;
        send_msg(msg200);
    }
}

void FTPDaemonThread::cmd_quit(const char *arg)
{
    send_msg(msg221);
    state = FTPD_QUIT;
}

void FTPDaemonThread::cmd_cwd(const char *arg)
{
    if (!vfs_chdir(vfs, arg)) {
        send_msg(msg250);
    } else {
        send_msg(msg550);
    }
}

void FTPDaemonThread::cmd_cdup(const char *arg)
{
    if (!vfs_chdir(vfs, "..")) {
        send_msg(msg250);
    } else {
        send_msg(msg550);
    }
}

void FTPDaemonThread::cmd_pwd(const char *arg)
{
    char *path;

    path = vfs_getcwd(vfs, NULL, 0);
    if (path) {
        send_msg(msg257PWD, path);
        free(path);
    }
}

// listType: 0 = list, 1 = nlst, 2 = mlst
void FTPDaemonThread::cmd_list_common(const char *arg, int listType)
{
    vfs_dir_t *vfs_dir;

    // Strip off arguments, as we don't support them
    while (*arg == '-') {
        while ((*arg) && (*arg != ' ')) {
            arg++;
        }
    }

    vfs_dir = vfs_opendir(vfs, arg, container_mode);

    if (!vfs_dir) {
        send_msg(msg451);
        return;
    }

    if (listType != 0)
        state = FTPD_NLST;
    else
        state = FTPD_LIST;

    if (!connection) {
        send_msg(msg425);
        vfs_closedir(vfs_dir);
        return;
    }

    send_msg(msg150);

    FTPTransferResult result = connection->directory(listType, vfs_dir);
    destroy_connection();

    send_msg(transfer_result_message(result));
}

void FTPDaemonThread::cmd_mlst(const char *arg)
{
    vfs_stat_t st;
    int result;
    const char *type;
    bool isDir;

    if ((arg == NULL) || (*arg == '\0')) { // No argument given
        result = vfs_stat(vfs, ".", &st);
        type = "cdir";
        isDir = true;
    } else {
        result = vfs_stat(vfs, arg, &st);
        if (VFS_ISDIR(st.st_mode)) {
            type = "dir";
            isDir = true;
        } else {
            type = "file";
            isDir = false;
        }
    }

    if (result) {
        send_msg(msg501);
        return;
    }
    char buffer[200];
    if (isDir)
        sprintf(buffer, "250- Listing %s\r\ntype=%s;modify=%04d%02d%02d%02d%02d%02d; %s\r\n250 End", st.name, type, st.year,
                st.month, st.day, st.hr, st.min, st.sec, st.name);
    else
        sprintf(buffer, "250- Listing %s\r\ntype=%s;size=%d;modify=%04d%02d%02d%02d%02d%02d; %s\r\n250 End", st.name, type,
                st.st_size, st.year, st.month, st.day, st.hr, st.min, st.sec, st.name);
    send_msg(buffer);
}

void FTPDaemonThread::cmd_mlsd(const char *arg)
{
    cmd_list_common(arg, 2);
}

void FTPDaemonThread::cmd_nlst(const char *arg)
{
    cmd_list_common(arg, 1);
}

void FTPDaemonThread::cmd_list(const char *arg)
{
    cmd_list_common(arg, 0);
}

void FTPDaemonThread::cmd_retr(const char *arg)
{
    vfs_file_t *vfs_file;
    vfs_stat_t st;

    int ret = vfs_stat(vfs, arg, &st);
    //printf("RET %d s%d m%d\n", ret, st.st_size, st.st_mode);
    if (!VFS_ISREG(st.st_mode)) {
        send_msg(msg550);
        return;
    }
    vfs_file = vfs_open(vfs, arg, "rb");
    if (!vfs_file) {
        send_msg(msg550);
        return;
    }

    if (!connection) {
        send_msg(msg425);
        vfs_close(vfs_file);
        return;
    }

    send_msg(msg150recv, arg, st.st_size);

    FTPTransferResult result = connection->sendfile(vfs_file);
    destroy_connection();

    send_msg(transfer_result_message(result));
}

void FTPDaemonThread::cmd_stor(const char *arg)
{
    if (!connection) {
        send_msg(msg425);
        return;
    }

    send_msg(msg150stor, arg);

    FTPTransferResult result = connection->receivefile(vfs, arg);
    destroy_connection();

    send_msg(transfer_result_message(result));
}

void FTPDaemonThread::cmd_noop(const char *arg)
{
    send_msg(msg200);
}

void FTPDaemonThread::cmd_syst(const char *arg)
{
    send_msg(msg214SYST, "UNIX");
}

void FTPDaemonThread::cmd_feat(const char *arg)
{
    send_msg(msg211);
}

void FTPDaemonThread::cmd_pasv(const char *arg)
{
    passive = 1;
    destroy_connection();

    for (int attempt = 0; attempt < 3; attempt++) {
        connection = new FTPDataConnection(this);
        if (connection && (connection->do_bind() == ERR_OK)) {
            return;
        }
        destroy_connection();
        if (attempt < 2) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }

    send_msg(msg425);
}

void FTPDaemonThread::cmd_abrt(const char *arg)
{
    destroy_connection();
    state = FTPD_IDLE;
}

void FTPDaemonThread::cmd_type(const char *arg)
{
    dbg_printf("Got TYPE -%s-\n", arg);
    send_msg(msg200);
}

void FTPDaemonThread::cmd_mode(const char *arg)
{
    dbg_printf("Got MODE -%s-\n", arg);
    send_msg(msg502);
}

void FTPDaemonThread::cmd_rnfr(const char *arg)
{
    if (arg == NULL) {
        send_msg(msg501);
        return;
    }
    if (*arg == '\0') {
        send_msg(msg501);
        return;
    }
    if (renamefrom)
        delete[] renamefrom;
    renamefrom = new char[strlen(arg) + 1];
    if (renamefrom == NULL) {
        send_msg(msg451);
        return;
    }
    strcpy(renamefrom, arg);
    state = FTPD_RNFR;
    send_msg(msg350);
}

void FTPDaemonThread::cmd_rnto(const char *arg)
{
    if (state != FTPD_RNFR) {
        send_msg(msg503);
        return;
    }
    state = FTPD_IDLE;
    if (arg == NULL) {
        send_msg(msg501);
        return;
    }
    if (*arg == '\0') {
        send_msg(msg501);
        return;
    }
    if (vfs_rename(vfs, renamefrom, arg)) {
        send_msg(msg450);
    } else {
        send_msg(msg250);
    }
    delete[] renamefrom;
    renamefrom = NULL;
}

void FTPDaemonThread::cmd_mkd(const char *arg)
{
    if (arg == NULL) {
        send_msg(msg501);
        return;
    }
    if (*arg == '\0') {
        send_msg(msg501);
        return;
    }
    char *path = vfs_getcwd(vfs, NULL, 0);
    if (!path) {
        send_msg(msg553);
        return;
    }
    if (vfs_mkdir(vfs, arg, VFS_IRWXU | VFS_IRWXG | VFS_IRWXO) != 0) {
        send_msg(msg553);
    } else {
        send_msg(msg257MKD, path, arg);
    }
    free(path);
}

void FTPDaemonThread::cmd_rmd(const char *arg)
{
    vfs_stat_t st;

    if (arg == NULL) {
        send_msg(msg501);
        return;
    }
    if (*arg == '\0') {
        send_msg(msg501);
        return;
    }
    if (vfs_stat(vfs, arg, &st) != 0) {
        send_msg(msg550);
        return;
    }
    if (!VFS_ISDIR(st.st_mode)) {
        send_msg(msg550);
        return;
    }
    if (vfs_rmdir(vfs, arg) != 0) {
        send_msg(msg550);
    } else {
        send_msg(msg250);
    }
}

void FTPDaemonThread::cmd_dele(const char *arg)
{
    vfs_stat_t st;

    if (arg == NULL) {
        send_msg(msg501);
        return;
    }
    if (*arg == '\0') {
        send_msg(msg501);
        return;
    }
    if (vfs_stat(vfs, arg, &st) != 0) {
        send_msg(msg550);
        return;
    }
    if (!VFS_ISREG(st.st_mode)) {
        send_msg(msg550);
        return;
    }
    if (vfs_remove(vfs, arg) != 0) {
        send_msg(msg550);
    } else {
        send_msg(msg250);
    }
}

void FTPDaemonThread::cmd_size(const char *arg)
{
    vfs_stat_t st;
    char buffer[20];

    if (arg == NULL) {
        send_msg(msg501);
        return;
    }
    if (*arg == '\0') {
        send_msg(msg501);
        return;
    }
    if (vfs_stat(vfs, arg, &st) != 0) {
        send_msg(msg550);
        return;
    }
    sprintf(buffer, msg213, st.st_size);
    send_msg(buffer);
}

struct ftpd_command {
    const char *cmd;
    func_t func;
};

int ftpd_last_unauthenticated_command_index = 2;  // "QUIT" (USER, PASS and QUIT are always allowed)

struct ftpd_command ftpd_commands[] = {
        // Commands always allowed
        "USER", &FTPDaemonThread::cmd_user,
        "PASS", &FTPDaemonThread::cmd_pass,
        "QUIT", &FTPDaemonThread::cmd_quit,

        // Commands only allowed after successful login
        "PORT", &FTPDaemonThread::cmd_port,
        "CWD",  &FTPDaemonThread::cmd_cwd,
        "CDUP", &FTPDaemonThread::cmd_cdup,
        "PWD",  &FTPDaemonThread::cmd_pwd,
        "XPWD", &FTPDaemonThread::cmd_pwd,
        "NLST", &FTPDaemonThread::cmd_nlst,
        "LIST", &FTPDaemonThread::cmd_list,
        "RETR", &FTPDaemonThread::cmd_retr,
        "STOR", &FTPDaemonThread::cmd_stor,
        "NOOP", &FTPDaemonThread::cmd_noop,
        "SYST", &FTPDaemonThread::cmd_syst,
        "ABOR", &FTPDaemonThread::cmd_abrt,
        "TYPE", &FTPDaemonThread::cmd_type,
        "MODE", &FTPDaemonThread::cmd_mode,
        "RNFR", &FTPDaemonThread::cmd_rnfr,
        "RNTO", &FTPDaemonThread::cmd_rnto,
        "MKD",  &FTPDaemonThread::cmd_mkd,
        "XMKD", &FTPDaemonThread::cmd_mkd,
        "RMD",  &FTPDaemonThread::cmd_rmd,
        "XRMD", &FTPDaemonThread::cmd_rmd,
        "DELE", &FTPDaemonThread::cmd_dele,
        "SIZE", &FTPDaemonThread::cmd_size,
        "PASV", &FTPDaemonThread::cmd_pasv,
        "MLST", &FTPDaemonThread::cmd_mlst,
        "MLSD", &FTPDaemonThread::cmd_mlsd,
        "FEAT", &FTPDaemonThread::cmd_feat,
        NULL };

void FTPDaemonThread::dispatch_command(char *text, int length)
{
    char cmd[5];
    char *pt = text;

    pt = &text[length - 1];
    while (((*pt == '\r') || (*pt == '\n')) && pt >= text)
        *pt-- = '\0';

    strncpy(cmd, text, 4);
    for (pt = cmd; isalpha(*pt) && pt < &cmd[4]; pt++)
        *pt = toupper(*pt);
    *pt = '\0';

    struct ftpd_command *ftpd_cmd;
    int ftpd_cmd_index = 0;
    for (ftpd_cmd = ftpd_commands; ftpd_cmd->cmd != NULL; ftpd_cmd++) {
        if (!strcmp(ftpd_cmd->cmd, cmd))
            break;
	++ftpd_cmd_index;
    }

    if(authenticated != 'y' && ftpd_cmd_index > ftpd_last_unauthenticated_command_index) {
        send_msg(msg530);
	return;
    }

    if (strlen(text) < (strlen(cmd) + 1))
        pt[0] = 0;
    else
        pt = &text[strlen(cmd) + 1];

    if (ftpd_cmd->func) {
        // this->*ftpd_cmd->func(pt);
        // func = ftpd_cmd->func;
        ((this)->*(ftpd_cmd->func))(pt);
    } else {
        send_msg(msg502);
    }
}

int FTPDaemonThread::open_dataconnection(bool passive)
{
    FTPDataConnection *conn = new FTPDataConnection(this); //, dataip, dataport);

    if (conn == NULL) {
        send_msg(msg451);
        return 1;
    }
    return 0;
}

// both the PORT and the PASV command create a new data socket and thread.
// In case of the PORT command, we will perform the connect to the specified IP/Port
// In case of the PASV command, we will bind a local socket, reply our IP address and socket port, and then switch to listen mode
FTPDataConnection::FTPDataConnection(FTPDaemonThread *parent)
{
    this->parent = parent;
    connected = 0;
    done = 0;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    actual_socket = -1;

    vfs_dirent = 0;
    vfs_file = 0;
}

FTPDataConnection::~FTPDataConnection()
{
    close_connection();
}

int FTPDataConnection::setup_connection()
{
    if (parent->passive) {
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv)) < 0) {
            puts("FTPD: failed to set accept timeout");
            return -1;
        }

        socklen_t clilen = sizeof(struct sockaddr_in);
        struct sockaddr_in cli_addr;
        actual_socket = -1;
        connected = 0;

        int s = accept(sockfd, (struct sockaddr * ) &cli_addr, &clilen);
        if (s < 0) {
            puts("FTPD: accept timed out or failed");
            return -1;
        }

        actual_socket = s;
        ftp_set_socket_timeout(actual_socket, 5, 0);
        connected = 1;
        return 0;
    }
    return connect_to(parent->dataip, parent->dataport);
}

void FTPDataConnection::close_connection()
{
    if (actual_socket >= 0)
        closesocket(actual_socket);
    if ((sockfd >= 0) && (actual_socket != sockfd))
        closesocket(sockfd);
    actual_socket = -1;
    sockfd = -1;
}

int FTPDataConnection::connect_to(ip_addr_t ip, uint16_t port) // active mode
{
    if (sockfd < 0)
        return -ENOTCONN;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    serv_addr.sin_addr.s_addr = ip.addr;

    if ( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        dbg_printf("FTPD Error : Connect Failed \n");
        return -ENOTCONN;
    }
    connected = 1;
    actual_socket = sockfd;
    ftp_set_socket_timeout(actual_socket, 5, 0);

    // now do your thing
    return ERR_OK;
}

int FTPDataConnection::do_bind(void)
{
    if (sockfd < 0)
        return -ENOTCONN;

    struct sockaddr_in serv_addr;
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_len = sizeof(serv_addr.sin_addr);

    int result = -1;
    int retry = bind_port_count;
    uint16_t port = 0;

    while (retry-- > 0) {
        port = parent->getBindPort();
        serv_addr.sin_port = htons(port);
        result = bind(sockfd, (struct sockaddr * ) &serv_addr, sizeof(serv_addr));
        if (result == 0) {
            break;
        }
        if (errno != EADDRINUSE) {
            break;
        }
    }

    if (result < 0) {
        puts("FTPD: ERROR on binding");
        return -ENOTCONN;
    }

    result = listen(sockfd, 2);
    if (result < 0) {
        return -ENOTCONN;
    }

    parent->send_msg(msg227, parent->my_ip[0], parent->my_ip[1], parent->my_ip[2], parent->my_ip[3], port >> 8, port & 0xFF);
    return 0;
}

FTPTransferResult FTPDataConnection::directory(int listType, vfs_dir_t *dir)
{
    FTPTransferResult result = FTP_TRANSFER_OK;

    if (setup_connection() != ERR_OK) {
        vfs_closedir(dir);
        return FTP_TRANSFER_SETUP_FAILED;
    }

    int len;
    vfs_stat_t st;
    vfs_dirent = vfs_readdir(dir);

    while(vfs_dirent) {
        vfs_stat_dirent(vfs_dirent, &st);

        switch (listType) {
        case 0:
            if (st.year == parent->current_year)
                len = sprintf(buffer, "-rw-rw-rw-   1 user     ftp  %11d %s %02d %02d:%02d %s\r\n", st.st_size,
                        month_table[st.month], st.day, st.hr, st.min, st.name);
            else
                len = sprintf(buffer, "-rw-rw-rw-   1 user     ftp  %11d %s %02d %5d %s\r\n", st.st_size,
                        month_table[st.month], st.day, st.year, st.name);
            if (VFS_ISDIR(st.st_mode))
                buffer[0] = 'd';
            break;
        case 1:
            len = sprintf(buffer, "%s\r\n", st.name);
            break;
        case 2:
            len = sprintf(buffer, "type=%s;size=%d;modify=%04d%02d%02d%02d%02d%02d; %s\r\n", VFS_ISDIR(st.st_mode) ? "dir" : "file",
                    st.st_size, st.year, st.month, st.day, st.hr, st.min, st.sec, st.name);
            break;
        default:
            len = sprintf(buffer, "Internal Error\r\n");
        }
        buffer[len] = 0;
        result = send_all(actual_socket, buffer, len);
        if (result != FTP_TRANSFER_OK) {
            break;
        }

        vfs_dirent = vfs_readdir(dir);
    }

    vfs_closedir(dir);
    return result;
}

FTPTransferResult FTPDataConnection::sendfile(vfs_file_t *file)
{
    FTPTransferResult result = FTP_TRANSFER_OK;

    if (setup_connection() != ERR_OK) {
        vfs_close(file);
        return FTP_TRANSFER_SETUP_FAILED;
    }

    int read;
    do {
        read = vfs_read(buffer, FTPD_DATA_BUFFER_SIZE, 1, file);
        if (read < 0) {
            result = FTP_TRANSFER_ABORTED;
            break;
        }
        if (read > 0) {
            result = send_all(actual_socket, buffer, read);
            if (result != FTP_TRANSFER_OK) {
                break;
            }
        }
    } while (read > 0);

    vfs_close(file);
    return result;
}

FTPTransferResult FTPDataConnection::receivefile(vfs_t *vfs, const char *path)
{
    FTPTransferResult result = FTP_TRANSFER_OK;
    vfs_file_t *file = NULL;

    if (setup_connection() != ERR_OK) {
        return FTP_TRANSFER_SETUP_FAILED;
    }

    int n;
    do {
        n = recv(actual_socket, buffer, FTPD_DATA_BUFFER_SIZE, 0);
        if (n > 0) {
            if (!file) {
                file = vfs_open(vfs, path, "wb");
                if (!file) {
                    result = FTP_TRANSFER_STORAGE_ERROR;
                    break;
                }
            }
            uint32_t written = vfs_write(buffer, n, 1, file);
            if (written != (uint32_t)n) {
                printf("Hmm.. written = %d. n = %d\n", written, n);
                result = FTP_TRANSFER_STORAGE_ERROR;
                break;
            }
        } else if (n < 0) {
            result = FTP_TRANSFER_ABORTED;
            break;
        }
    } while (n > 0);

    if (file) {
        vfs_close(file);
    }
    return result;
}

#include "init_function.h"
InitFunction init_ftpd("FTP Daemon", [](void *_obj, void *_param) { ftpd = new FTPDaemon(); }, NULL, NULL, 101); // global that causes us to exist
