/*
 *	anysurrect_io.h
 *      CopyRight (C) 2006-2008, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <byteswap.h>
#include <endian.h>
#elif defined(_WIN32)
#include <sys/param.h>
#include "misc.h"
#endif

#include "frags_funcs.h"

static inline any_size_t fd_size()
{
	return file_frags_list->size;
}

static inline any_off_t fd_seek(any_off_t offset, int whence)
{
	if (whence==SEEK_CUR)
		offset += cur_offset;

	if (whence==SEEK_END)
		offset += fd_size();

	cur_offset = offset;

	return cur_offset;
}

struct io_buffer {
	char	*buffer;
	any_off_t	start;
	int		size;
};

extern struct io_buffer io_buffer;
extern int fd;
extern uint32_t blocksize;
extern uint64_t blocksize64;
extern int 	 log2blocksize;
extern uint32_t bitsblocksize;
extern uint64_t bitsblocksize64;
extern uint32_t nbitsblocksize;
extern uint64_t nbitsblocksize64;

any_off_t _fd_seek(any_off_t offset, int whence);

static inline void set_blocksize(uint32_t s_blocksize)
{
	blocksize = s_blocksize;
	log2blocksize = 0;
	while (s_blocksize > 1)
	{
		log2blocksize++;
		s_blocksize>>=1;
	}
	bitsblocksize = (1UL<<log2blocksize) - 1;
	nbitsblocksize = ~bitsblocksize;

	blocksize64 = blocksize;
	bitsblocksize64 = (1ULL<<log2blocksize) - 1;
	nbitsblocksize64 = ~bitsblocksize64;
}

static inline uint32_t get_blocksize()
{
	return blocksize;
}

static inline uint64_t get_blocksize64()
{
	return blocksize64;
}

static inline int get_log2blocksize()
{
	return log2blocksize;
}

static inline uint32_t get_bitsblocksize()
{
	return bitsblocksize;
}

static inline uint32_t get_nbitsblocksize()
{
	return nbitsblocksize;
}

static inline uint64_t get_bitsblocksize64()
{
	return bitsblocksize64;
}

static inline uint64_t get_nbitsblocksize64()
{
	return nbitsblocksize64;
}

static inline unsigned long get_block()
{
	return file_frags_list->frag.fr_start;
}

#if defined(__linux__)
#define READ(a,b,c) read(a, b, c)

#ifdef __cplusplus
#define CLOSE(a) ::close(a)
#else
#define CLOSE(a) close(a)
#endif

#elif defined(_WIN32)
extern struct io_buffer bufs[1024];

static inline ssize_t my_read(int fd, void *buf, size_t count)
{

#define BSIZE	(512*1024)
#define HBSIZE	(256*1024)
#define BSIZEM	(512*1024-1)
#define HBSIZEM	(256*1024-1)

	if ( fd >= 1024 ) return read(fd, buf, count);

	if (!bufs[fd].buffer)
	{
		bufs[fd].buffer = (char*) malloc(BSIZE);
		bufs[fd].start = 0;
		bufs[fd].size = 0;
	}

	any_size_t c = count;
	any_size_t p, r;

	any_off_t from = lseek64(fd, 0, SEEK_CUR);

	p = from;

	while (c)
	{
		if ( p > (bufs[fd].start + bufs[fd].size - 1) ||
				p < bufs[fd].start ||
				bufs[fd].size == 0 )
		{
			if ( p > bufs[fd].start && p < (bufs[fd].start + BSIZEM) )
			{
				lseek(fd, bufs[fd].start +
						bufs[fd].size, 
						SEEK_SET);

				ssize_t z = ( (p - (bufs[fd].start + 
						bufs[fd].size) ) >> 10) + 1;

				ssize_t sz =
					read(fd, bufs[fd].buffer +
							bufs[fd].size, 
							z<<10);

				if (sz<0)
					return sz;

				if (sz==0)
				{
					lseek64(fd, p, SEEK_SET);
					return count - c;
				}

				bufs[fd].size += sz;
			}
			else
			if ( p > (bufs[fd].start + BSIZEM) &&
			  p < (bufs[fd].start + BSIZEM + HBSIZE) )
			{
				if (bufs[fd].size < BSIZE)
				{
					lseek(fd, bufs[fd].start +
							bufs[fd].size, 
							SEEK_SET);

					ssize_t sz =
						read(fd, bufs[fd].buffer +
								bufs[fd].size, 
								BSIZE -
								bufs[fd].size);

					if (sz<0)
						return sz;

					if (sz==0)
					{
						lseek64(fd, p, SEEK_SET);
						return count - c;
					}

					bufs[fd].size += sz;
				}

				any_off_t end = bufs[fd].start + bufs[fd].size;
				any_off_t new_start = (p & (~(1<<10))) - HBSIZE;
				any_off_t new_end = (p & (~(1<<10))) + 1024;

				memmove( bufs[fd].buffer,
						bufs[fd].buffer + 
						(new_start - bufs[fd].start),
						end - new_start );

				bufs[fd].start = new_start;
				bufs[fd].size = end - new_start;

				lseek(fd, bufs[fd].start +
						bufs[fd].size, 
						SEEK_SET);

				ssize_t sz =
					read(fd, bufs[fd].buffer +
							bufs[fd].size, 
							new_end - end);

				if (sz<0) return sz;

				if (sz==0)
				{
					lseek64(fd, p, SEEK_SET);
					return count - c;
				}

				bufs[fd].size += sz;
			}
			else
			if ( p > (bufs[fd].start - HBSIZEM) &&
			  p < (bufs[fd].start + HBSIZEM) )
			{
				any_off_t end = bufs[fd].start + bufs[fd].size;
				any_off_t new_start = p & (~(1<<10)) - HBSIZE;
				any_off_t new_size = min_t( any_off_t,
						end - new_start, BSIZE);
				any_off_t new_end = new_start + new_size;

				memmove( bufs[fd].buffer + new_size - 
						(new_end - bufs[fd].start),
						bufs[fd].buffer,
						new_end - bufs[fd].start );

				lseek(fd, new_start, 
						SEEK_SET);

				ssize_t sz =
					read(fd, bufs[fd].buffer, 
							bufs[fd].start - 
							new_start);

				bufs[fd].start = new_start;
				bufs[fd].size = new_size;

				if (sz<0) return sz;

				if (sz==0)
				{
					lseek64(fd, p, SEEK_SET);
					return count - c;
				}
			}
			else
			{
				lseek(fd, p, SEEK_SET);

				bufs[fd].start = p;
				bufs[fd].size =
					read(fd, bufs[fd].buffer, 1024);

				if (bufs[fd].size<0)
					return bufs[fd].size;

				if (bufs[fd].size==0)
				{
					lseek64(fd, p, SEEK_SET);
					return count - c;
				}
			}
		}

		r = min_t(any_size_t, c, bufs[fd].size - (p - bufs[fd].start) );

		switch(r)
		{
			case 0:
				lseek64(fd, p, SEEK_SET);
				return count - c;
				printf ("%lld, %lld, %lld, %lld\n", c, bufs[fd].size - (p - bufs[fd].start),
						p, bufs[fd].start);
				exit (1);
				break;

			default:
				memcpy ((char*)buf + (p-from),
						(char*)bufs[fd].buffer + (p - bufs[fd].start), 
						r);
		}

		c-=r;
		p+=r;
	}

	lseek64(fd, p, SEEK_SET);

	return count;
};

static inline int my_close(int fd)
{
	if ( fd < 1024 && bufs[fd].buffer )
	{
		free(bufs[fd].buffer);
	        bufs[fd].buffer	= NULL;
		bufs[fd].start = 0;
		bufs[fd].size = 0;
	}

#ifdef __cplusplus
	return ::close(fd);
#else
	return close(fd);
#endif
}

#define READ(a,b,c) my_read(a, b, c)
#define CLOSE(a) my_close(a)
#endif


#if __WORDSIZE == 32 
static inline any_ssize_t fd_read32(void *buf, uint32_t count)
{
	uint32_t c = count;
	uint32_t p, r;
	
	uint32_t from = cur_offset & ((1ULL<<31)-1);
	uint32_t bfstart = io_buffer.start & ((1ULL<<31)-1);
	uint64_t cur_offset_high = cur_offset & ~((1ULL<<31)-1);

	p = from;
	while (c)
	{
		if ( p>(bfstart + io_buffer.size - 1) ||
		  p<bfstart || io_buffer.size==0 )
		{
			uint32_t bp = p & get_nbitsblocksize();
			_fd_seek(cur_offset_high + bp, SEEK_SET);

			bfstart = bp;
			io_buffer.start = cur_offset_high + bfstart;

			io_buffer.size = READ (fd, io_buffer.buffer, get_blocksize());
			if (io_buffer.size<0) 
				return io_buffer.size;
			
			if (io_buffer.size==0)
			{
				cur_offset = cur_offset_high + p;
				return count - c;
			}
		}

		r = min_t(uint32_t, c, io_buffer.size - (p - bfstart) );

		switch(r)
		{
			case 0:
				cur_offset = cur_offset_high + p;
				return count - c;
				printf ("%lu, %lu, %lu, %lu\n", (unsigned long) c, 
						(unsigned long) io_buffer.size - (p - bfstart),
						(unsigned long) p, 
						(unsigned long) bfstart);
				exit (1);
				break;

			case 1:
				*(char*)((char*)buf + (p-from)) = 
					*(char*)(io_buffer.buffer + (p - bfstart));
				break;
			case 2:
				*(uint16_t*)((char*)buf + (p-from)) = 
					*(uint16_t*)(io_buffer.buffer + (p - bfstart));
				break;
			case 4:
				*(uint32_t*)((char*)buf + (p-from)) = 
					*(uint32_t*)(io_buffer.buffer + (p - bfstart));
				break;
			default:
				memcpy ((char*)buf + (p-from),
						(char*)io_buffer.buffer + (p - bfstart), 
						r);
		}

		c-=r;
		p+=r;
	}

	cur_offset = cur_offset_high + p;
	return count;
}
#endif

static inline any_ssize_t fd_read(void *buf, any_size_t count)
{
#if __WORDSIZE == 32 
	if ( !(count>>31) )
		return fd_read32(buf, count & ((1ULL<<31)-1));
#endif

	any_size_t c = count;
	any_size_t p, r;
	
	any_off_t from = cur_offset;

	p = from;
	while (c)
	{
		if ( p>(io_buffer.start + io_buffer.size - 1) ||
		  p<io_buffer.start || io_buffer.size==0 )
		{
			any_size_t bp = p & get_nbitsblocksize64();
			_fd_seek(bp, SEEK_SET);

			io_buffer.start = bp;

			io_buffer.size = READ (fd, io_buffer.buffer, get_blocksize());
			if (io_buffer.size<0) 
				return io_buffer.size;
			
			if (io_buffer.size==0)
			{
				cur_offset = p;
				return count - c;
			}
		}

		r = min_t(any_size_t, c, io_buffer.size - (p - io_buffer.start) );

		switch(r)
		{
			case 0:
				cur_offset = p;
				return count - c;
				printf ("%lld, %lld, %lld, %lld\n", c, io_buffer.size - (p - io_buffer.start),
						p, io_buffer.start);
				exit (1);
				break;

			case 1:
				*(char*)((char*)buf + (p-from)) = 
					*(char*)((char*)io_buffer.buffer + (p - io_buffer.start));
				break;
			case 2:
				*(uint16_t*)((char*)buf + (p-from)) = 
					*(uint16_t*)((char*)io_buffer.buffer + (p - io_buffer.start));
				break;
			case 4:
				*(uint32_t*)((char*)buf + (p-from)) = 
					*(uint32_t*)((char*)io_buffer.buffer + (p - io_buffer.start));
				break;
			default:
				memcpy ((char*)buf + (p-from),
						(char*)io_buffer.buffer + (p - io_buffer.start), 
						r);
		}

		c-=r;
		p+=r;
	}

	cur_offset = p;
	return count;
}

static inline int read_byte(uint8_t *value)
{
	int res=0;
	res=fd_read(value, 1);
	if (!res) return 1;

	return 0;
}

static inline int read_beshort(uint16_t *value)
{
	int res=0;
	res=fd_read(value, 2);
	if (!res) return 1;
		
#if	BYTE_ORDER==LITTLE_ENDIAN
	*value = bswap_16(*value);
#endif
	return 0;
}

static inline int read_belong(uint32_t *value)
{
	int res=0;
	res=fd_read(value, 4);
	if (!res) return 1;
		
#if	BYTE_ORDER==LITTLE_ENDIAN
	*value = bswap_32(*value);
#endif
	return 0;
}

static inline int read_belong64(uint64_t *value)
{
	int res=0;
	res=fd_read(value, 8);
	if (!res) return 1;
		
#if	BYTE_ORDER==LITTLE_ENDIAN
	*value = bswap_64(*value);
#endif
	return 0;
}

static inline int read_leshort(uint16_t *value)
{
	int res=0;
	res=fd_read(value, 2);
	if (!res) return 1;
		
#if	BYTE_ORDER==BIG_ENDIAN
	*value = bswap_16(*value);
#endif
	return 0;
}

static inline int read_lelong(uint32_t *value)
{
	int res=0;
	res=fd_read(value, 4);
	if (!res) return 1;
		
#if	BYTE_ORDER==BIG_ENDIAN
	*value = bswap_32(*value);
#endif
	return 0;
}

static inline int read_lelong64(uint64_t *value)
{
	int res=0;
	res=fd_read(value, 8);
	if (!res) return 1;
		
#if	BYTE_ORDER==BIG_ENDIAN
	*value = bswap_64(*value);
#endif
	return 0;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

