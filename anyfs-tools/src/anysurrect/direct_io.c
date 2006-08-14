/*
 *	direct_io.c
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#define _LARGEFILE64_SOURCE

#include "any.h"
#include "super.h"

#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "direct_io.h"

#include "block_map.h"
#include "progress.h"
#include "version.h"

extern int fd;
extern any_size_t blocksize;
extern any_size_t device_blocks;
extern any_size_t get_blocksize();

any_off_t direct_start = 0;

void fd_set_direct_start(any_off_t offset)
{
	direct_start = offset;
}

any_off_t fd_get_direct_start()
{
	return direct_start;
}

any_size_t fd_size_dr()
{
	return device_blocks*get_blocksize();
}

any_off_t fd_seek_dr(any_off_t offset, int whence)
{
	if (whence==SEEK_SET) offset+=direct_start;
	return lseek64(fd, offset, whence) - direct_start;
}

any_ssize_t fd_read_dr(void *buf, any_size_t count)
{
	return read(fd, buf, count);
}

int read_byte_dr(uint8_t *value)
{
	int res=0;
	res=fd_read_dr(value, 1);
	if (!res) return 1;

	return 0;
}

int read_beshort_dr(uint16_t *value)
{
	int res=0;
	res=fd_read_dr(value, 2);
	if (!res) return 1;
		
#if	BYTE_ORDER==LITTLE_ENDIAN
	char *s = (char*) value;
	char b = s[0];
	s[0] = s[1];
	s[1] = b;
#endif
	return 0;
}

int read_belong_dr(uint32_t *value)
{
	int res=0;
	res=fd_read_dr(value, 4);
	if (!res) return 1;
		
#if	BYTE_ORDER==LITTLE_ENDIAN
	char *s = (char*) value;
	char b = s[0];
	s[0] = s[3];
	s[3] = b;

	b = s[1];
	s[1] = s[2];
	s[2] = b;
#endif
	return 0;
}

int read_leshort_dr(uint16_t *value)
{
	int res=0;
	res=fd_read_dr(value, 2);
	if (!res) return 1;
		
#if	BYTE_ORDER==BIG_ENDIAN
	char *s = (char*) value;
	char b = s[0];
	s[0] = s[1];
	s[1] = b;
#endif
	return 0;
}

int read_lelong_dr(uint32_t *value)
{
	int res=0;
	res=fd_read_dr(value, 4);
	if (!res) return 1;
		
#if	BYTE_ORDER==BIG_ENDIAN
	char *s = (char*) value;
	char b = s[0];
	s[0] = s[3];
	s[3] = b;

	b = s[1];
	s[1] = s[2];
	s[2] = b;
#endif
	return 0;
}

