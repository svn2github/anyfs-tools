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
	LIST_STRING("magic", 2, {"\037\213", "\037\236", NULL});
	fd_seek(0, SEEK_SET);

	char buf[BUFFER_SIZE];
	int n;

	pid_t child_pid;
	int zombie_pid;

	int status = 0;

	int stdindes[2];
	int stderrdes[2];

	pipe (stdindes);
	pipe (stderrdes);

	if ( !( child_pid=fork() ) )
	{
		struct sigaction sa;
		memset (&sa, 0, sizeof (sa));
		sa.sa_handler = SIG_DFL;
		sigaction (SIGINT, &sa, NULL);
		sigaction (SIGSEGV, &sa, NULL);
		sigaction (SIGUSR1, &sa, NULL);

		dup2(stdindes[0], fileno(stdin));
		dup2(stderrdes[1], fileno(stdout));

		exit ( gzip() );
	}

	fcntl(stdindes[1], F_SETFL, O_NONBLOCK);

	int eofd = 0;

	for (;;) {
		zombie_pid = waitpid(child_pid, &status, WNOHANG);
		if (zombie_pid==child_pid) break;

		n = fd_read(buf, BUFFER_SIZE);
		if (!n && !eofd)
		{
			eofd = 1;
			memset (buf, 255, BUFFER_SIZE);
		}

		if (eofd) n=BUFFER_SIZE;

		any_ssize_t w = write(stdindes[1], buf, n);
		if (w<0 && errno==EAGAIN)
		{
			w=0;
			usleep(100);
		}

		fd_seek(w-n, SEEK_CUR);
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
}
