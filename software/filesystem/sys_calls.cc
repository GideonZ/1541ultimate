/*
 * sys_calls.cc
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
//#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>


extern "C" int *__errno()
{
	return &(_impure_ptr->_errno);
}


extern "C" void outbyte(int c);

extern "C" int getpid(void)
{
	return 1;
}

extern "C" int kill(int pid, int sig)
{
	errno = EINVAL;
	return -1;
}

extern "C" int write(int file, char *ptr, int len)
{
	   int todo;

	   for (todo = 0; todo < len; todo++)
	   {
		   outbyte( *ptr++ );
	   }

	// Implement your write code here, this is used by puts and printf for example
	return len;
}

extern "C" int close(int file)
{
	return -1;
}


extern "C" int fstat(int file, struct stat *st)
{
	st->st_mode = S_IFCHR;
	return 0;
}

extern "C" int isatty(int file)
{
	return 1;
}

extern "C" int lseek(int file, int ptr, int dir)
{
	return 0;
}

extern "C" int read(int file, char *ptr, int len)
{
	return 0;
}

extern "C" int open(char *path, int flags, ...)
{
	printf("Open stub called with '%s' and flags = %4x\n", path, flags);
	// Pretend like we always fail
	return -1;
}

extern "C" int wait(int *status)
{
	errno = ECHILD;
	return -1;
}

extern "C" int unlink(char *name)
{
	errno = ENOENT;
	return -1;
}

extern "C" int link(char *old, char *newname)
{
	errno = EMLINK;
	return -1;
}

extern "C" int fork(void)
{
	errno = EAGAIN;
	return -1;
}

extern "C" int execve(char *name, char **argv, char **env)
{
	errno = ENOMEM;
	return -1;
}

extern "C" int fsync(int filedes)
{
	errno = EBADF;
	return -1;
}
