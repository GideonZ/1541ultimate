/*
 * Copyright (c) 2002 Florian Schulze.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the authors nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * ftpd.h - This file is part of the FTP daemon for lwIP
 *
 */

#ifndef __FTPD_H__
#define __FTPD_H__

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "FreeRTOS.h"
#include "task.h"
//#include "semphr.h"
#include "indexed_list.h"
#include "vfs.h"

#define ERR_OK 0

class FTPDaemon
{
	static void ftp_listen_task(void *a);
public:
	TaskHandle_t listenTaskHandle;

	FTPDaemon();
	~FTPDaemon() { }

	int listen_task(void);
};

#define COMMAND_BUFFER_SIZE 1024

class FTPDataConnection;
class FTPDaemonThread;

typedef void (FTPDaemonThread::*func_t)(const char *args);

class FTPDaemonThread
{
	int socket;
	uint8_t my_ip[4];
	uint8_t your_ip[4];
	uint16_t your_port;
        int d64asdir;

	enum ftpd_state_e {
		FTPD_USER,
		FTPD_PASS,
		FTPD_IDLE,
		FTPD_NLST,
		FTPD_LIST,
		FTPD_RETR,
		FTPD_RNFR,
		FTPD_STOR,
		FTPD_QUIT
	};

	func_t func;

	IndexedList<FTPDataConnection *>data_connections;

	enum ftpd_state_e state;
	vfs_t *vfs;

	struct ip_addr dataip;
	uint16_t dataport;
	int passive;
	FTPDataConnection *connection;

	char *renamefrom;
	int current_year;
	char command_buffer[COMMAND_BUFFER_SIZE];


	friend class FTPDaemon;
	friend class FTPDataConnection;

	static void run(void *a);
	void send_msg(const char *a, ...);
	void dispatch_command(char *a, int length);
	int open_dataconnection(bool passive);
public:
	FTPDaemonThread(int sock, uint32_t addr, uint16_t port);
	~FTPDaemonThread() { }

	int handle_connection(void);
	uint16_t getBindPort();

	// Commands
	void cmd_user(const char *arg);
	void cmd_pass(const char *arg);
	void cmd_port(const char *arg);
	void cmd_quit(const char *arg);
	void cmd_cwd(const char *arg);
	void cmd_cdup(const char *arg);
	void cmd_pwd(const char *arg);
	void cmd_list_common(const char *arg, int shortlist);
	void cmd_nlst(const char *arg);
	void cmd_list(const char *arg);
	void cmd_retr(const char *arg);
	void cmd_stor(const char *arg);
	void cmd_noop(const char *arg);
	void cmd_syst(const char *arg);
	void cmd_pasv(const char *arg);
	void cmd_abrt(const char *arg);
	void cmd_type(const char *arg);
	void cmd_mode(const char *arg);
	void cmd_rnfr(const char *arg);
	void cmd_rnto(const char *arg);
	void cmd_mkd(const char *arg);
	void cmd_rmd(const char *arg);
	void cmd_dele(const char *arg);
	void cmd_size(const char *arg);
    void cmd_mlst(const char *arg);
    void cmd_mlsd(const char *arg);
    void cmd_feat(const char *arg);
};

class FTPDataConnection
{
	int connected;
	int done;
	vfs_dirent_t *vfs_dirent;
	vfs_file_t *vfs_file;
	FTPDaemonThread *parent;
	int sockfd;
	int actual_socket;
	char buffer[1024];

	int setup_connection();
	int connect_to(struct ip_addr ip, uint16_t port);
	static void accept_data(void *); // task
	TaskHandle_t acceptTaskHandle;
	TaskHandle_t spawningTask;

public:
	FTPDataConnection(FTPDaemonThread *parent);
	~FTPDataConnection() { }
	int do_bind(void);
	void close_connection();

	void directory(int listType, vfs_dir_t *dir);
	void sendfile(vfs_file_t *file);
	bool receivefile(vfs_file_t *file);
};
#endif				/* __FTPD_H__ */
