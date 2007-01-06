/*
 *	anysurrect_io.h
 *      CopyRight (C) 2006-2007, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern any_off_t	cur_offset;

any_size_t fd_size();

static inline any_off_t fd_seek(any_off_t offset, int whence)
{
	if (whence==SEEK_CUR)
		offset += cur_offset;

	if (whence==SEEK_END)
		offset += fd_size();

	cur_offset = offset;

	return cur_offset;
}

struct frags_list {
	struct any_file_fragment frag;
	signed long	offnext;
	long whole;
	unsigned long	num_frags;
};

extern unsigned long *block_bitmap;
extern struct frags_list *frags_list;
extern struct frags_list *file_template_frags_list;
extern struct frags_list *file_frags_list;

struct io_buffer {
	char	*buffer;
	any_off_t	start;
	uint32_t	size;
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

			io_buffer.size = read (fd, io_buffer.buffer, get_blocksize());
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
				*(char*)(buf + (p-from)) = 
					*(char*)(io_buffer.buffer + (p - bfstart));
				break;
			case 2:
				*(uint16_t*)(buf + (p-from)) = 
					*(uint16_t*)(io_buffer.buffer + (p - bfstart));
				break;
			case 4:
				*(uint32_t*)(buf + (p-from)) = 
					*(uint32_t*)(io_buffer.buffer + (p - bfstart));
				break;
			default:
				memcpy (buf + (p-from),
						io_buffer.buffer + (p - bfstart), 
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

			io_buffer.size = read (fd, io_buffer.buffer, get_blocksize());
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
				*(char*)(buf + (p-from)) = 
					*(char*)(io_buffer.buffer + (p - io_buffer.start));
				break;
			case 2:
				*(uint16_t*)(buf + (p-from)) = 
					*(uint16_t*)(io_buffer.buffer + (p - io_buffer.start));
				break;
			case 4:
				*(uint32_t*)(buf + (p-from)) = 
					*(uint32_t*)(io_buffer.buffer + (p - io_buffer.start));
				break;
			default:
				memcpy (buf + (p-from),
						io_buffer.buffer + (p - io_buffer.start), 
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
	char *s = (char*) value;
	char b = s[0];
	s[0] = s[1];
	s[1] = b;
#endif
	return 0;
}

static inline int read_belong(uint32_t *value)
{
	int res=0;
	res=fd_read(value, 4);
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

static inline int read_leshort(uint16_t *value)
{
	int res=0;
	res=fd_read(value, 2);
	if (!res) return 1;
		
#if	BYTE_ORDER==BIG_ENDIAN
	char *s = (char*) value;
	char b = s[0];
	s[0] = s[1];
	s[1] = b;
#endif
	return 0;
}

static inline int read_lelong(uint32_t *value)
{
	int res=0;
	res=fd_read(value, 4);
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

