/*
 *	anysurrect.c
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#define _LARGEFILE64_SOURCE

#include "any.h"
#include "super.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <sys/ioctl.h>
#include <linux/fd.h>

#include <dlfcn.h>

#include <assert.h>

#include <bitops.h>
#include <endian.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "anysurrect.h"

#include "new_inode.h"
#include "block_map.h"
#include "progress.h"
#include "version.h"

#define BLKGETSIZE _IO(0x12,96) /* return device size /512 (long *arg) */

const char* default_list_types =
#include "surrectlist.conf"
;

const char* ext2fs_info_list_types =
"filesystem_info_ext2fs_direct_blocks_links "
"filesystem_info_ext2fs_indirect_blocks_links "
"filesystem_info_ext2fs_double_indirect_blocks_links "
;

char* list_types = NULL;

const char * program_name = "anysurrect";

char * libs = "";
int num_libs;
void **libs_handles;

struct progress_struct progress;

int list_all_types = 0;
int verbose = 0;
int quiet = 0;
int fromfirst = 0;

const char *pathprefix = "";
const char *device_name;
const char *inode_table;
const char *input_inode_table = NULL;
unsigned long input_blocksize = 0;

any_size_t device_blocks = 0;

mode_t dir_umask = 0002;
mode_t file_umask = 0002;

const mode_t default_mode = 0666;
const int default_text = 0;
const char *default_indicator = "UNKNOWN";

typedef char *surrect_function_t();

surrect_function_t** surrects;
const mode_t **modes;
const int **texts;
const char ***indicators;
int num_types = 0; 

static void usage(void)
{
	fprintf(stderr, _(
"Usage: %s [-b blocksize] [-i input_inode_table] [-p path_prefix]\n"
"\t[-u file_umask] [-U dir_umask] [-fqvV] [-g plug-ins]\n"
"\t[-e] [-t list_of_types] [-l] device inode_table\n"),
			program_name);
	exit(1);
}

char *concat_strings(int n, ...)
{
	va_list ap;
	int length=1;
	int i;
	va_start (ap, n);
	for (i=0; i<n; i++) length+=strlen(va_arg(ap,char *));
	va_end (ap);
	char *concat=malloc(sizeof(char)*length);
	if (!concat) return NULL;

	concat[0]='\0';
	va_start (ap, n);
	for (i=0; i<n; i++) strcat(concat,va_arg(ap,char *));
	va_end (ap);
	return concat;
}

struct frags_list {
	struct any_file_fragment frag;
	signed long	offnext;
	long whole;
	unsigned long	num_frags;
};

unsigned long *block_bitmap;
struct frags_list *frags_list;
struct frags_list *file_template_frags_list;
struct frags_list *file_frags_list;

unsigned long blocks_before_frag;
struct frags_list *cur_frag;

int create_frags_list(unsigned long *block_bitmap,
		unsigned long blocks,
		struct frags_list **pfrags_list)
{
	unsigned long block=0;
	struct frags_list *new;
	struct frags_list *prev = NULL;
	
	for (; block<blocks; block++)
	{
		unsigned long start = block;
		unsigned long length = 0;
		for (; block<blocks && !test_bit(block, block_bitmap); 
				block++, length++);
		if (length)
		{
			new = malloc(sizeof(struct frags_list));
			if ( !new )
			{
				fprintf(stderr, _("Not enough memory\n"));
				exit(1);
			}
			(*pfrags_list) = (void*)((char*)new - (char*)prev);
			prev = new;

			new->frag.fr_start =  start;
			new->frag.fr_length = length;
			new->offnext = 0;
			new->whole = -1;
			pfrags_list = (void*)&new->offnext;
		}
	}

	return 0;
}

#define NEXT_FRAG_WA_OFS(frag, offnext) 		\
	(void*)( (offnext) ? (char*)frag + offnext : 0 )

#define NEXT_FRAG_OFS(frag, offnext) 		\
	( frag = NEXT_FRAG_WA_OFS(frag, offnext) )

#define NEXT_FRAG(frag) NEXT_FRAG_OFS(frag, frag->offnext)
#define NEXT_FRAG_WA(frag) NEXT_FRAG_WA_OFS(frag, frag->offnext)

int copy_frags_list(struct frags_list *from, struct frags_list **pto)
{
	struct frags_list *new;
	struct frags_list *prev = NULL;

	assert(from);
	if (from->whole!=-1)
	{
		struct frags_list *whole = 
			malloc(sizeof(struct frags_list)*from->num_frags);
		if (!whole)
		{
			fprintf(stderr, _("Not enough memory\n"));
			exit(1);
		}

		memcpy(whole, from - from->whole,
				sizeof(struct frags_list)*from->num_frags);

		(*pto) = whole + from->whole;

		return 0;
	}

	struct frags_list *from_orig = from;
	
	unsigned long num_frags = 0;
	
	for (; from; NEXT_FRAG(from) )
		num_frags++;

	from = from_orig;

	struct frags_list *whole = 
		malloc(sizeof(struct frags_list)*num_frags);
	if (!whole)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	
	struct frags_list *whole_part =  whole;

	for (; from; NEXT_FRAG(from) )
	{
		new = whole_part++;

		(*pto) = (void*)((char*)new - (char*)prev);
		prev = new;

		new->frag = from->frag;
		new->offnext = 0;
		new->whole = new - whole;
		new->num_frags = num_frags;
		pto = (void*)&new->offnext;
	}

	return 0;
}

int free_frags_list(struct frags_list *frags_list)
{
	signed long offnext = 0;

	if (frags_list->whole!=-1)
	{
		free(frags_list - frags_list->whole);
		return 0;
	}

	for (; frags_list; NEXT_FRAG_OFS(frags_list, offnext) )
	{
		offnext = frags_list->offnext;
		free(frags_list);
	}

	return 0;
}


int cut_frags(struct frags_list **pfrags_list, unsigned long from,
		unsigned long blocks)
{
	unsigned long block = 0;
	unsigned long fr_length;
	unsigned long fr_start;

	struct frags_list *frags_list = *pfrags_list;

	struct frags_list *frag_begin_cut = NULL;
	unsigned long new_fbc_length = 0;
	
	struct frags_list *frag_end_cut = NULL;
	unsigned long new_fec_start = 0;
	unsigned long new_fec_length = 0;

	if (!blocks) return 0;
	
	for (; frags_list; NEXT_FRAG(frags_list))
	{
		fr_length = frags_list->frag.fr_length;
		if ( (block + fr_length)>from ) {
			frag_begin_cut = frags_list;
			new_fbc_length = from - block;
			break;
		}
		
		block += fr_length;
	}
	
	for (; frags_list; NEXT_FRAG(frags_list))
	{
		fr_length = frags_list->frag.fr_length;
		if ( (block + fr_length)>(from+blocks) ) {
			fr_start = frags_list->frag.fr_start;
			frag_end_cut = frags_list;
			new_fec_start = fr_start + from+blocks-block;
			new_fec_length = fr_start + fr_length
				- new_fec_start;
			break;
		}
		
		block += fr_length;
	}

	assert(frag_begin_cut);

	frag_begin_cut->frag.fr_length = new_fbc_length;

	if (frag_end_cut)
	{
		frag_end_cut->frag.fr_start = new_fec_start;
		frag_end_cut->frag.fr_length = new_fec_length;
	}

	if (frag_begin_cut!=frag_end_cut)
	{
		frags_list = NEXT_FRAG_WA(frag_begin_cut);
		signed long offnext = 0;

		for (; frags_list!=frag_end_cut;
				NEXT_FRAG_OFS(frags_list, offnext))
		{
			offnext = frags_list->offnext;
			if (frags_list->whole==-1)
				free(frags_list);
		}

		frag_begin_cut->offnext = (char*)frag_end_cut - 
			(char*)frag_begin_cut;
	}

	while ( (*pfrags_list) && !(*pfrags_list)->frag.fr_length )
	{
		struct frags_list *next = NEXT_FRAG_WA( (*pfrags_list) );
		if ((*pfrags_list)->whole==-1)
			free(*pfrags_list);
		else if (!next)
			free( (*pfrags_list) - (*pfrags_list)->whole );
		(*pfrags_list) = next;
	}

	return 0;
}

int pick_frags(struct frags_list **pfrags_list, unsigned long from,
		unsigned long blocks)
{
	unsigned long block = 0;
	unsigned long fr_length;
	unsigned long fr_start;
	
	struct frags_list *frags_list = *pfrags_list;

	struct frags_list *start_frags_list = frags_list;

	struct frags_list *frag_begin_pick = NULL;
	unsigned long new_fbp_start = 0;
	unsigned long new_fbp_length = 0;
	
	struct frags_list *frag_end_pick = NULL;
	unsigned long new_fep_length = 0;
	
	for (; frags_list; NEXT_FRAG(frags_list) )
	{
		fr_length = frags_list->frag.fr_length;
		if ( (block + fr_length)>from ) {
			fr_start = frags_list->frag.fr_start;
			frag_begin_pick = frags_list;
			new_fbp_start = fr_start + from-block;
			new_fbp_length = fr_start + fr_length
				- new_fbp_start;
			break;
		}
		
		block += fr_length;
	}
	
	for (; frags_list; NEXT_FRAG(frags_list) )
	{
		fr_length = frags_list->frag.fr_length;
		if ( (block + fr_length)>(from+blocks) ) {
			frag_end_pick = frags_list;
			new_fep_length = from+blocks - block;
			break;
		}
		
		block += fr_length;
	}

	if (!frag_begin_pick || !frag_end_pick)
	{
		return 0;
		fprintf(stderr, "Error in pick_frags\n");
		exit(1);
	}

	frag_begin_pick->frag.fr_start = new_fbp_start;
	frag_begin_pick->frag.fr_length = new_fbp_length;

	frag_end_pick->frag.fr_length = new_fep_length;

	frags_list = start_frags_list;
	signed long offnext = 0;

	for (; frags_list!=frag_begin_pick; 
			NEXT_FRAG_OFS(frags_list, offnext) )
	{
		offnext = frags_list->offnext;
		if (frags_list->whole==-1)
			free(frags_list);
	}
	
	frags_list = NEXT_FRAG_WA(frag_end_pick);
	offnext = 0;

	for (; frags_list; NEXT_FRAG_OFS(frags_list, offnext) )
	{
		offnext = frags_list->offnext;
		if (frags_list->whole==-1)
			free(frags_list);
	}

	*pfrags_list = frag_begin_pick;
	frag_end_pick->offnext = 0;
	
	return 0;
}

int frags_list2array (struct frags_list *frags_list, 
		struct any_file_frags **pfile_frags)
{
	struct frags_list *frags_list_p = frags_list;
	struct any_file_fragment *file_frag;
	
	(*pfile_frags) = calloc (sizeof(struct any_file_frags), 1);
	if (!(*pfile_frags))
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	
	for (; frags_list_p; NEXT_FRAG(frags_list_p) )
		(*pfile_frags)->fr_nfrags++;

	(*pfile_frags)->fr_frags = 
		malloc( sizeof(struct any_file_fragment) *
				(*pfile_frags)->fr_nfrags );
		
	file_frag = (*pfile_frags)->fr_frags;
	
	for (; frags_list; NEXT_FRAG(frags_list), 
			file_frag++)
		(*file_frag) = frags_list->frag;

	return 0;
}

/*
int redirect_frags(struct frags_list *frags_list, unsigned long after,
		unsigned long block);
 */

int fd;
any_size_t blocksize;

struct io_buffer {
	char	*buffer;
	any_off_t	start;
	any_size_t	size;
};

struct io_buffer io_buffer = {NULL, -1, 0};

void set_blocksize(any_ssize_t s_blocksize)
{
	blocksize = s_blocksize;
}

int set_block(any_ssize_t s_block)
{
	cut_frags(&file_template_frags_list, 0, s_block);
	io_buffer.size = 0;

	/*
	printf ("%d, %d\n", file_template_frags_list->frag.fr_start,
			file_template_frags_list->frag.fr_length);
*/
	return (file_template_frags_list)?0:1;
}

any_size_t get_blocksize()
{
	return blocksize;
}

unsigned long get_block()
{
	return file_frags_list->frag.fr_start;
}

any_size_t fd_size()
{
	any_size_t blocks = 0;
	struct frags_list *frags_list = file_frags_list;
	
	for (; frags_list; NEXT_FRAG(frags_list) )
		blocks += frags_list->frag.fr_length;

	return blocks*get_blocksize();
}

any_off_t fd_seek(any_off_t offset, int whence)
{
	static any_off_t cur_offset = 0;
	
	if (whence==SEEK_CUR)
		offset += cur_offset;
	
	if (whence==SEEK_END)
		offset += fd_size();
	
	if ( blocks_before_frag > offset || !cur_frag )
	{
		blocks_before_frag = 0;
		cur_frag = file_frags_list;
	}

	while ( ( ( blocks_before_frag + cur_frag->frag.fr_length ) *
					get_blocksize() )<=offset )
	{
		blocks_before_frag += cur_frag->frag.fr_length;
		NEXT_FRAG(cur_frag);
		if ( !cur_frag )
		{
			if ( ( blocks_before_frag*get_blocksize() )<offset )
				return -1;
			lseek64(fd, 0, SEEK_END);
			cur_offset = offset;
			return cur_offset;
		}
	}

	lseek64(fd, (any_size_t) cur_frag->frag.fr_start * get_blocksize() + offset - 
			(any_size_t) blocks_before_frag * get_blocksize() , 
			whence);
	cur_offset = offset;
	
	return cur_offset;
}

any_ssize_t fd_read(void *buf, any_size_t count)
{
	any_size_t c = count;
	any_size_t p, r;
	
	any_off_t from = fd_seek(0, SEEK_CUR);

	p = from;
	while (c)
	{
		if ( p>(io_buffer.start + io_buffer.size - 1) ||
		  p<io_buffer.start || io_buffer.size==0 )
		{
			fd_seek(p, SEEK_SET);
			io_buffer.start = p;
			io_buffer.size = read (fd, io_buffer.buffer, get_blocksize());
			if (io_buffer.size<0) 
				return 0;
			
			if (io_buffer.size==0)
				return count - c;
		}

		r = min_t(any_size_t, c, io_buffer.size - (p - io_buffer.start) );
		if (r==0) 
		{
			printf ("%lld, %lld\n", c, io_buffer.size - (p - io_buffer.start));
			exit (1);
		}

		memcpy (buf + (p-from),
				io_buffer.buffer + (p - io_buffer.start), 
				r);

		c-=r;
		p+=r;
	}

	fd_seek(p, SEEK_SET);
	return count;
}

int read_byte(uint8_t *value)
{
	int res=0;
	res=fd_read(value, 1);
	if (!res) return 1;

	return 0;
}

int read_beshort(uint16_t *value)
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

int read_belong(uint32_t *value)
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

int read_leshort(uint16_t *value)
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

int read_lelong(uint32_t *value)
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

void anysurrect_file(struct any_sb_info *info, char *dirn, mode_t mode)
{
	int res;
	struct any_inode *dir_inode;
	struct any_inode *inode;
	struct any_dirent **pdirent;
	struct any_dirent *newdirent;
	uint32_t newino;
	uint32_t dirino;
	char name_buffer[1024];

	char *dir = concat_strings(2, pathprefix, dirn);
	
	res = mkpathino(dir, 1, info, &dirino);

	free(dir);
	
	res = any_new_inode(info, S_IFREG | ( mode & (~file_umask & 0777)  ), 
			NULL, dirino, &newino);
	if (res) 
	{
		fprintf (stderr, _("error while creating inode\n"));
		exit(1);
	}

	res = snprintf (name_buffer, 1024, "inode_%d_block_%ld",
			newino, get_block() );
	if (res<0)
	{
		fprintf(stderr, _("error while format filename\n"));
		exit(1);
	}

	newdirent = calloc(sizeof(struct any_dirent), 1);
	if (!newdirent)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}

        dir_inode = info->si_inode_table + dirino;
	
	pdirent = &dir_inode->i_info.dir->d_dirent;

	for (; *pdirent; pdirent=&(*pdirent)->d_next);

	dir_inode->i_info.dir->d_ndirents++;
	(*pdirent) = newdirent;
	(*pdirent)->d_name = strdup(name_buffer);
	(*pdirent)->d_inode = newino;
	
	inode = info->si_inode_table + newino;
	inode->i_size = fd_seek(0, SEEK_CUR);
	
	pick_frags(&file_frags_list, 0,
			( fd_seek(0, SEEK_CUR) + 
			   get_blocksize() - 1   )/get_blocksize() );

/*	printf ("%d, %d\n", file_frags_list->frag.fr_start,
			file_frags_list->frag.fr_length);
*/	
	frags_list2array (file_frags_list, &inode->i_info.file_frags);
/*
	printf ("%d\n", inode->i_info.file_frags->fr_nfrags);
	printf ("%d, %d\n", inode->i_info.file_frags->fr_frags->fr_start,
			inode->i_info.file_frags->fr_frags->fr_length);
			*/
}

void anysurrect_fromblock(struct any_sb_info *info)
{
	char *mes;
	any_size_t max_size=1;

	static char *buffer = NULL;
	int type;
        int i;

	if (!buffer)
	{
		buffer = malloc(1024);
		if (!buffer)
		{
			fprintf (stderr, _("Not enough memory\n"));
			exit(1);
		}

		setvbuf(stdout, buffer, _IOLBF, 1024);
	}

	char typeline[21];

	int pend = __fpending(stdout);
	if ( (1024 - pend) < 32 )
	{
		fflush(stdout);
		pend = __fpending(stdout);
	}

	static unsigned long *SKIP_TO_BLOCK = NULL;
	if (!SKIP_TO_BLOCK) 
	{
		SKIP_TO_BLOCK = calloc(num_types, sizeof(unsigned long) );
		
		if (!SKIP_TO_BLOCK)
		{
			fprintf (stderr, _("Not enough memory\n"));
			exit(1);
		}
	}
	
	for (type=0; type<num_types; type++)
	{
		mode_t mode = *modes[type];
		int text = *texts[type];

		if (!quiet) {
			memset (typeline, ' ', 20);
			typeline[20] = '\0';
			int s = snprintf (typeline, 20, " [%s]", 
					*indicators[type]);
			if (s>=0 && s<=20) typeline[s]=' ';

			if (!type)
				printf("%s", typeline);
			else
			{
				int pend2 = __fpending(stdout);
				if ( (pend2-20)==pend )
				{
					memcpy(buffer + pend, typeline, 20);
				}
				else
				{
					for (i = 0; i < 20; i++)
						fputc('\b', stdout);

					pend = __fpending(stdout);
					if ( (1024 - pend) < 32 )
					{
						fflush(stdout);
						pend = __fpending(stdout);
					}

					printf("%s", typeline);
				}
			}
		}

		copy_frags_list (file_template_frags_list, &file_frags_list);
		blocks_before_frag = 0;
		cur_frag = file_frags_list;
		fd_seek(0, SEEK_SET);
		if (!text || (get_block()>=SKIP_TO_BLOCK[type]) ) {
			mes = surrects[type](); 
			if (mes) { 
				char* dupmes = strdup(mes);
				if (verbose) printf("file %s, block=%ld, size=%llx\n",
						mes, get_block(), fd_seek(0, SEEK_CUR));
				max_size = max_t(any_size_t, max_size, fd_seek(0, SEEK_CUR) );
				anysurrect_file(info, dupmes, mode);
				free(dupmes);
			}
			else {	
				SKIP_TO_BLOCK[type] = get_block() + 
					(fd_seek(0, SEEK_CUR)+get_blocksize()-1)/get_blocksize();
			}
		}
		free_frags_list (file_frags_list);
	}

	for (i = 0; i < 20; i++)
		fputc('\b', stdout);

	set_block( (max_size+get_blocksize()-1)/get_blocksize() );
}

static void PRS(int argc, const char *argv[])
{
	int             c;
	int show_version_only = 0;
	char *          tmp;

	list_types = (char*) default_list_types;

	while ((c = getopt (argc, (char**)argv,
		     "b:i:p:u:U:qvVfg:et:l")) != EOF) {
		switch (c) {
		case 'b':
			input_blocksize = strtol(optarg, &tmp, 10);

			int log2 = 1;
			int b = input_blocksize;
			while ( (b = b>>1)>1 ) log2++;
			for (; log2; log2--) b = b<<1;
			if (b!=input_blocksize || input_blocksize<512 || *tmp) 
			{
				fprintf (stderr, 
						_("bad new blocksize. It is must be power of 2 and not less than 512\n"));
				exit(1);
			}
			break;

		case 'i':
			input_inode_table = optarg;
			break;

		case 'p':
			pathprefix = optarg;
			break;
			
		case 'u':
			file_umask = strtol(optarg, &tmp, 8);

			if (file_umask>0777 || *tmp) 
			{
				fprintf (stderr, 
_("Illegal mode for file umask. It must be 3 octal digits.\n"));
				exit(1);
			}
			break;
			
		case 'U':
			dir_umask = strtol(optarg, &tmp, 8);

			if (file_umask>0777 || *tmp) 
			{
				fprintf (stderr, 
_("Illegal mode for directory umask. It must be 3 octal digits.\n"));
				exit(1);
			}
			break;

		case 'q':
			quiet = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'V':
			show_version_only++;
			break;
		case 'f':
			fromfirst = 1;
			break;

		case 'g':
			libs = optarg;
			break;
			
		case 'e':
			list_types = (char*) ext2fs_info_list_types;
			break;

		case 't':
			list_types = optarg;
			break;

		case 'l':
			list_all_types = 1;
			break;

		default:
			usage();
		}
	}

	if ( ( optind >= (argc-1) ) && !show_version_only && !list_all_types)
		usage();

	if (!quiet || show_version_only)
		fprintf (stderr, "anysurrect %s (%s)\n", ANYFSTOOLS_VERSION,
				ANYFSTOOLS_DATE);

	if (show_version_only) {
		exit(0);
	}
	
	if (!list_all_types)
	{
		device_name = argv[optind++];
		inode_table = argv[optind++];
	}

	list_types = strdup(list_types);
	libs = strdup(libs);
}

void sigusr1_handler (int signal_number)
{       
	fflush(stdout);
}

void sigint_handler (int signal_number)
{       
	printf ("\nuser cancel\n");
	fflush(stdout);

	_exit(1);
}

void sigsegv_handler (int signal_number)
{       
	fflush(stdout);
	fprintf (stderr, "Segmentation fault\n");

	abort();
}

int main (int argc, const char *argv[])
{
	int r;
	uint32_t rootino;
	struct any_sb_info *info;
	unsigned long bitmap_l;
	unsigned long *block_bitmap;

	char *begin;
	char *end;

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
#endif

	struct sigaction sa;
	memset (&sa, 0, sizeof (sa));
	sa.sa_handler = &sigint_handler;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGSEGV);
	sigaddset(&sa.sa_mask, SIGUSR1);
	sigaction (SIGINT, &sa, NULL);

	struct sigaction sa1;
	memset (&sa1, 0, sizeof (sa1));
	sa1.sa_handler = &sigsegv_handler;
	sa1.sa_flags = SA_RESTART;
	sigemptyset(&sa1.sa_mask);
	sigaddset(&sa1.sa_mask, SIGINT);
	sigaddset(&sa1.sa_mask, SIGSEGV);
	sigaddset(&sa1.sa_mask, SIGUSR1);
	sigaction (SIGSEGV, &sa1, NULL);

	struct sigaction sa2;
	memset (&sa2, 0, sizeof (sa2));
	sa2.sa_handler = &sigusr1_handler;
	sa2.sa_flags = SA_RESTART;
	sigemptyset(&sa2.sa_mask);
	sigaddset(&sa2.sa_mask, SIGINT);
	sigaddset(&sa2.sa_mask, SIGSEGV);
	sigaddset(&sa2.sa_mask, SIGUSR1);
	sigaction (SIGUSR1, &sa2, NULL);

	PRS(argc, argv);
	
	num_libs = 1;
	begin = libs;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		num_libs++;
		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}
	
	libs_handles = (void**) 
		MALLOC( sizeof(void*) * num_libs );

	libs_handles[0] = dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);
	if (!libs_handles[0])
	{
		fprintf( stderr, "Error dlopen() main handle: %s\n",
				dlerror() );
		exit(1);
	}
	
	int lib = 1;
	begin = libs;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		char old = end[0];
		end[0] = '\0';

		libs_handles[lib] = dlopen(begin, RTLD_LAZY | RTLD_GLOBAL);
		if (!libs_handles[lib])
		{
			fprintf( stderr, "Error dlopen() %s: %s\n",
					begin, dlerror() );
			exit(1);
		}

		end[0] = old;
		lib++;

		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}

	if (list_all_types)
	{
		char self[1024];
		char buf[1024];
		
		r = readlink("/proc/self", self, 1023);
		if (r<0) exit(1);

		self[r] = '\0';
			
		r = snprintf(buf, 1024,
"for j in `for i in %s %s; do "
	"cat /proc/%s/maps | "
		"awk -- '/ r-xp.*'${i//\\//\\\\\\/}'$/ {print $6}';"
	" done`; do " 
	"echo; echo \"FILE SURRECTERS EXPORTED BY \\\"${j##*/}\\\" MODULE:\"; "
	"nm --defined-only -D \"$j\" | awk -- '/_surrect$/ "
	"{sub(/_surrect$/, \"\"); ORS=\" \"; print $3}'; echo; "
"done",
			program_name, libs, self);
		if (r<0) exit(1);

		pid_t child;
		if ( !( child = fork() ) )
		{
			execl ("/bin/sh", "/bin/sh", "-c", buf, NULL);
			exit(1);
		}

		waitpid(child, NULL, 0);

		exit(0);
	}

	begin = list_types;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		num_types++;
		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}
	
	surrects = (surrect_function_t**) 
		MALLOC( sizeof(surrect_function_t*) * num_types );
	modes = (const mode_t**)
		MALLOC( sizeof(mode_t*) * num_types );
	texts = (const int**)
		MALLOC( sizeof(int*) * num_types );
	indicators = (const char***)
		MALLOC( sizeof(char**) * num_types );

	int type = 0;
	begin = list_types;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		void *handle = NULL;
		int n;
		char buf[1024];

		char old = end[0];
		end[0] = '\0';

		n=snprintf(buf, 1024, "%s_surrect", begin);
		if (n<0) exit(1);

		for (lib=0; lib<num_libs; lib++)
		{
			surrects[type] = dlsym(libs_handles[lib], buf);
			if (surrects[type])
			{
				handle = libs_handles[lib];
				break;
			}
		}

		if (!handle)
		{
			fprintf (stderr,
					_("can't find %s surrecter function\n"),
					begin);
			exit(1);
		}

		n=snprintf(buf, 1024, "%s_mode", begin);
		if (n<0) exit(1);

		modes[type] = dlsym(handle, buf);
		if (!modes[type])
			modes[type] = &default_mode;

		n=snprintf(buf, 1024, "%s_text", begin);
		if (n<0) exit(1);

		texts[type] = dlsym(handle, buf);
		if (!texts[type])
			texts[type] = &default_text;

		n=snprintf(buf, 1024, "%s_indicator", begin);
		if (n<0) exit(1);

		indicators[type] = dlsym(handle, buf);
		if (!indicators[type])
			indicators[type] = &default_indicator;

		end[0] = old;
		type++;

		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}

	fd = open(device_name, O_RDONLY | O_LARGEFILE);
	if (fd==-1) {
		fprintf(stderr, _("Error opening file\n"));
		exit(1);
	}

	struct stat64 st;
	fstat64(fd, &st);
	if (st.st_mode & S_IFBLK)
	{
		if (ioctl(fd, BLKGETSIZE, &st.st_size) < 0) {
			perror("BLKGETSIZE");
			close(fd);
			return -EIO;
		}
		st.st_size*=512;
	}

	if (!input_inode_table)
	{
		if (!input_blocksize)
		{
			set_blocksize(512);

			while ( st.st_size/get_blocksize() > 0xFFFFFF )
				set_blocksize(get_blocksize()*2);
		}
		else
		{
			set_blocksize(input_blocksize);

			if ( st.st_size/get_blocksize() > 0xFFFFFF )
			{
				fprintf(stderr,
_("Blocksize very small for this device. Recommend to use greater blocksize\n"));
			}
		}

		uint32_t inodes = st.st_size/( 4*get_blocksize() ) + 256;

		r = alloc_it(&info, get_blocksize(), inodes);
		if (r<0) goto out;

		r = any_new_inode(info, S_IFDIR | (~dir_umask & 0777), NULL, 0, &rootino);
		if (r) goto free_out;

		if (rootino!=1)
		{
			fprintf (stderr, _("Error creating root inode\n"));
			exit(1);
		}
	}
	else
	{
		r = read_it (&info, (void*)input_inode_table);
		if (r)
		{
			fprintf(stderr,
					_("Error while reading inode table: %s\n"),
					(errno)?strerror(errno):_("bad format"));
			goto out;
		}
		
		if (input_blocksize && input_blocksize!=info->si_blocksize)
		{
			fprintf(stderr,
_("Specified input inode table has %lu blocksize,\n"
"  but specified blocksize by -b option is %lu\n"
"Use 'reblock' for changing blocksize of inode_table\n"),
	info->si_blocksize, input_blocksize);
			exit(1);
		}
		
		set_blocksize(info->si_blocksize);
	}
	
	any_size_t blocks = (st.st_size+get_blocksize()-1)/get_blocksize();
	device_blocks = blocks;

	if (!blocks)
	{
			fprintf(stderr, _("It is not funny at all! I can do nothing with empty file.\n"));
			exit(1);
	}

	bitmap_l = ( blocks +
			sizeof(unsigned long) - 1 )/sizeof(unsigned long);

	/*allocate memory for block bitmap*/
	block_bitmap =  (typeof(block_bitmap))
		calloc(bitmap_l*sizeof(unsigned long), 1);
	if (!block_bitmap)      
	{ 
		fprintf(stderr, _("Not enough memory\n"));
		exit(1); 
	}

	r = fill_block_bitmap (info, block_bitmap, blocks);
	if (r) exit(r);

	if (fromfirst) clear_bit(0, block_bitmap);
	
	create_frags_list(block_bitmap, blocks, &frags_list);

	io_buffer.buffer = malloc(get_blocksize());
	if (!io_buffer.buffer)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	
	copy_frags_list (frags_list, &file_template_frags_list);

	if (quiet)
		memset(&progress, 0, sizeof(progress));
	else
		progress_init(&progress, _("search through all blocks: "),
				blocks);
	
	for ( ; !set_block(0); )
	{
		progress_update(&progress, 
				file_template_frags_list->frag.fr_start);
		anysurrect_fromblock(info);
	}

	progress_close(&progress);

	r = write_it (info, (void*)inode_table);
	if (r)
	{
		fprintf(stderr,
				_("Error while writing inode table: %s\n"),
				(errno)?strerror(errno):_("format error"));
		goto free_out;
	}

free_out:
	free_it (info);

out:
	close(fd);
	
	for (lib=0; lib<num_libs; lib++)
	{
		if ( dlclose(libs_handles[lib]) )
		{
			fprintf( stderr, "Error dlclose(): %s\n",
					dlerror() );
			exit(1);
		}
	}

	return r;
}
