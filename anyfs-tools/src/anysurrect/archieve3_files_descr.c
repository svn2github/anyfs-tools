/*
 *      archieve3_files_descr.c
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "anysurrect.h"
#include "archieve3_files_descr.h"

#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

extern int gzip ();

#define BUFFER_SIZE	4096

char *archieve_GZIP_surrect()
{
#define ERROR_VALUE     0
	int res;

	LIST_STRING("magic", 2, {"\037\213", "\037\236", NULL});
	fd_seek(0, SEEK_SET);

	char buf[BUFFER_SIZE];
	int n;

	fd_set rfds;
	struct timeval tv;
	int retval;

	tv.tv_sec = 0;
	tv.tv_usec = 100;

	pid_t child_pid;
	int zombie_pid;

	int status = 0;

	int stdindes[2];
	int stderrdes[2];

	pipe (stdindes);
	pipe (stderrdes);

	if ( !( child_pid=fork() ) )
	{
		dup2(stdindes[0], fileno(stdin));
		dup2(stderrdes[1], fileno(stdout));

		exit ( gzip() );
	}

	int eofd = 0;

	for (;;) {
		zombie_pid = waitpid(child_pid, &status, WNOHANG);
		if (zombie_pid==child_pid) break;

		FD_ZERO(&rfds);
		FD_SET(stdindes[0], &rfds);

		retval = select(stdindes[0]+1, &rfds, NULL, NULL, &tv);

		if (!retval)
		{
			n = fd_read(buf, BUFFER_SIZE);
			if (!n && !eofd)
			{
				eofd = 1;
				memset (buf, 255, BUFFER_SIZE);
			}

			if (eofd) n=BUFFER_SIZE;

			write(stdindes[1], buf, n);
		}
		else usleep(100);
	}

	any_ssize_t size = 0;

	close (stdindes[0]);
	close (stdindes[1]);

	if (WEXITSTATUS(status))
	{
	    read(stderrdes[0], buf, BUFFER_SIZE);
	    size = strtol(buf, NULL, 10);

	    if (size<=0) return ERROR_VALUE;

	    fd_seek (size, SEEK_SET);

	    close (stderrdes[0]);
	    close (stderrdes[1]);

	    return "archieve/GZIP";
	}
	else 
	{
	    close (stderrdes[0]);
	    close (stderrdes[1]);
	    return ERROR_VALUE;
	}

	return ERROR_VALUE;
#undef  ERROR_VALUE
}
