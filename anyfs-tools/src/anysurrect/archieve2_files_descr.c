/*
 *      archieve2_files_descr.c
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 *	
 *	From bzip2.c (part of bzip2):
 *	Copyright (C) 1996-2005 Julian R Seward.
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
#include "archieve2_files_descr.h"

#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include "bzlib.h"

#include <stdint.h>

typedef unsigned char   Bool;
typedef unsigned char   UChar;
typedef int		Int32;

#define True  ((Bool)1)
#define False ((Bool)0)

/*---------------------------------------------*/

int verbosity = 0;
int smallMode = 0;
int noisy = 0;
const char* progName = "bzip2test";
char *inName = "stdin";

static void panic( char* s )
{ 
	fprintf ( stderr,
		"\n panic while testing bzip2 archieve:\n"
		"%s\n", s );
	exit(1);
}

static void configError()
{ 
	fprintf ( stderr,
		"\n bzip2 is not configured correctly for this platform!\n" );
	exit(1);
}

static void ioError ()
{
	fprintf ( stderr,
		"\n i/o Error while testing bzip2 archieve\n" );
	exit(1);
}

static void outOfMemory()
{
	fprintf ( stderr,
		"\n couldn't allocate enough memory\n" );
	exit(1);
}

/*---------------------------------------------*/
static Bool myfeof ( FILE* f )
{
	Int32 c = fgetc ( f );
	if (c == EOF) return True;
	ungetc ( c, f );
	return False;
}

/*---------------------------------------------*/
static int testStream ( FILE *zStream )
{
	BZFILE* bzf = NULL;
	Int32   bzerr, bzerr_dummy, ret, nread, streamNo, i;
	UChar   obuf[5000];
	UChar   unused[BZ_MAX_UNUSED];
	Int32   nUnused;
	void*   unusedTmpV;
	UChar*  unusedTmp;

	nUnused = 0;
	streamNo = 0;

	if (ferror(zStream)) goto errhandler_io;

	while (True) {

		bzf = BZ2_bzReadOpen ( 
				&bzerr, zStream, verbosity, 
				(int)smallMode, unused, nUnused
				);
		if (bzf == NULL || bzerr != BZ_OK) goto errhandler;
		streamNo++;

		while (bzerr == BZ_OK) {
			nread = BZ2_bzRead ( &bzerr, bzf, obuf, 5000 );
			if (bzerr == BZ_DATA_ERROR_MAGIC) goto errhandler;
		}
		if (bzerr != BZ_STREAM_END) goto errhandler;

		BZ2_bzReadGetUnused ( &bzerr, bzf, &unusedTmpV, &nUnused );
		if (bzerr != BZ_OK) panic ( "test:bzReadGetUnused" );

		unusedTmp = (UChar*)unusedTmpV;
		for (i = 0; i < nUnused; i++) unused[i] = unusedTmp[i];

		BZ2_bzReadClose ( &bzerr, bzf );
		if (bzerr != BZ_OK) panic ( "test:bzReadGetUnused" );
		if (nUnused == 0 && myfeof(zStream)) break;

	}

	if (ferror(zStream)) goto errhandler_io;
	ret = fclose ( zStream );
	if (ret == EOF) goto errhandler_io;

	printf ("0\n");

	return True;

errhandler:

	BZ2_bzReadClose ( &bzerr_dummy, bzf );

	switch (bzerr) {
		case BZ_CONFIG_ERROR:
			configError(); break;
		case BZ_IO_ERROR:
errhandler_io:
			ioError(); break;
		case BZ_DATA_ERROR:

			return False;
		case BZ_MEM_ERROR:
			outOfMemory();
		case BZ_UNEXPECTED_EOF:
			return False;
		case BZ_DATA_ERROR_MAGIC:
			if (zStream != stdin) fclose(zStream);
			if (streamNo == 1) {
				return False;
			} else {
				fgetc(zStream);
				printf ("%d\n", nUnused+1);
				return True;       
			}
		default:
			panic ( "test:unexpected error" );
	}

	panic ( "test:end" );
	return True; /*notreached*/
}

/*---------------------------------------------*/
#define BUFFER_SIZE	BZ_MAX_UNUSED

char *archieve_BZIP2_surrect()
{
	EX_STRING("magic", "BZh");
	fd_seek(0, SEEK_SET);

	char buf[BUFFER_SIZE];
	int n;
	long writes=0;

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
		struct sigaction sa;
		memset (&sa, 0, sizeof (sa));
		sa.sa_handler = SIG_DFL;
		sigaction (SIGINT, &sa, NULL);
		sigaction (SIGSEGV, &sa, NULL);
		sigaction (SIGUSR1, &sa, NULL);

		dup2(stdindes[0], fileno(stdin));
		dup2(stderrdes[1], fileno(stdout));
		setvbuf (stdin, NULL, _IONBF, 0);

		exit ( testStream(stdin) );
	}

	int eofd = 0;

	for (;;) {
		zombie_pid = waitpid(child_pid, &status, WNOHANG);
		if (zombie_pid==child_pid) break;

		FD_ZERO(&rfds);
		FD_SET(stdindes[0], &rfds);

		do retval = select(stdindes[0]+1, &rfds, NULL, NULL, &tv);
		while ( retval < 0 && errno == EINTR );

		if (!retval)
		{
			n = fd_read(buf, BUFFER_SIZE);
			if (!n && !eofd)
			{
				eofd = 1;
				memset (buf, 0, BUFFER_SIZE);
			}

			if (eofd) n=BUFFER_SIZE;

			any_ssize_t w = write(stdindes[1], buf, n);
			writes+=w;
		}
		else usleep(100);
	}

	while (1)
	{
		FD_ZERO(&rfds);
		FD_SET(stdindes[0], &rfds);

		do retval = select(stdindes[0]+1, &rfds, NULL, NULL, &tv);
		while ( retval < 0 && errno == EINTR );

		if (retval)
		{
			n = read(stdindes[0], buf, BUFFER_SIZE);
			writes -= n;
		}
		else break;
	}

	any_ssize_t size = 0;

	close (stdindes[0]);
	close (stdindes[1]);

	if (WEXITSTATUS(status))
	{
		read(stderrdes[0], buf, BUFFER_SIZE);
		size = writes - strtol(buf, NULL, 10);

		fd_seek (size, SEEK_SET);

		close (stderrdes[0]);
		close (stderrdes[1]);
		return "archieve/BZIP2";
	}
	else 
	{
		close (stderrdes[0]);
		close (stderrdes[1]);
		return ERROR_VALUE;
	}

	return ERROR_VALUE;
}
