/*
 * reblock.c
 * Copyright (C) 2005 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 *
 */

#define _LARGEFILE64_SOURCE

#include "block_map.h"
#include "any.h"
#include "super.h"
#include "progress.h"
#include "version.h"
#include "bitops.h"

#include <sys/types.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <sys/ioctl.h>
#include <linux/fd.h>

#define BLKGETSIZE _IO(0x12,96) /* return device size /512 (long *arg) */

const char * program_name = "reblock";

int verbose = 0;
int quiet = 0;
int noaction = 0;

const char *inode_table;
const char *device_name;
unsigned long new_blocksize;

static void usage(void)
{
	fprintf(stderr, _("Usage: %s [-nqvV] inode_table device blocksize\n"
			"Please READ 'man %s' BEFORE using\n"
			"Use option -n before real action\n"),
			program_name, program_name);
        exit(1);
}

char *buffer = NULL;
int bfl = 0;

struct blocks_info
{
	unsigned long blocksize;
	unsigned long blocks_count;
	unsigned long *block_bitmap;
};

int test_block(unsigned long b, struct blocks_info *blocks_info,
		unsigned long new_blocksize)
{
	unsigned long i, j;
	int k = new_blocksize/blocks_info->blocksize;
	if (k<=1) return 0;

	i = b*k;
	int s = 0;
	for (j=0; j<k; j++)
	{
		if ((i+j)>=blocks_info->blocks_count) continue;
		s += test_bit(i+j, blocks_info->block_bitmap);
	}

	return s;
}

void set_scaled_bit(unsigned long b, struct blocks_info *blocks_info,
		unsigned long new_blocksize)
{
	unsigned long i, j;
	int k = new_blocksize/blocks_info->blocksize;
	if (k<=1) return;

	i = b*k;
	for (j=0; j<k; j++)
	{
		set_bit(i+j, blocks_info->block_bitmap);
	}
}

int read_blocks(int file, unsigned long block,
		unsigned long nblocks,
		char *buffer,
		struct blocks_info *blocks_info)
{
	unsigned long i;
	
	if (!block)
	{
		memset(buffer, 0, nblocks * blocks_info->blocksize);
		return nblocks * blocks_info->blocksize;
	}
	
	lseek64 (file, block * blocks_info->blocksize, SEEK_SET);
	
	for (i=0; i<nblocks; i++)
		clear_bit(block + i, blocks_info->block_bitmap);

	if (noaction) return nblocks * blocks_info->blocksize;
		
	return read (file, buffer, nblocks * blocks_info->blocksize);
}

unsigned long write_block(int file, unsigned long block,
		int sparse, char *buffer,
		struct blocks_info *blocks_info,
		unsigned long new_blocksize)
{
	if (sparse)
	{
		unsigned long i;
		for (i=0; i<new_blocksize; i++)
		{
			if (buffer[i])
			{
				sparse=0;
				break;
			}
		}

		if (sparse) return 0;
	}
	
	if ( test_block(block, blocks_info, new_blocksize) )
	{
		int k = new_blocksize/blocks_info->blocksize;
		
		block = (find_first_zero_bit(
				blocks_info->block_bitmap,
				blocks_info->blocks_count)+k-1)/k;

		while ( block < (blocks_info->blocks_count/k)
		     && test_block(block, blocks_info, new_blocksize) )
		{
			block = (find_next_zero_bit(
					blocks_info->block_bitmap,
					blocks_info->blocks_count,
					(block+1)*k)+k-1)/k;
		}
			
		if ( block >= (blocks_info->blocks_count/k) )
			return 0;
	}

	set_scaled_bit(block, blocks_info, new_blocksize);
	
	lseek64 (file, block * new_blocksize, SEEK_SET);
	if (!noaction)
		write (file, buffer, new_blocksize);
	return block;
}

struct frags_list {
	struct any_file_fragment frag;
	struct frags_list        *next;
};

int reblock(struct any_sb_info *info, int file,
		struct blocks_info *blocks_info,
		unsigned long new_blocksize)
{
	struct progress_struct progress;
	int r = 0;
	unsigned long frag;
	unsigned long mpos;
	
	int k = new_blocksize / blocks_info->blocksize;
	int m = blocks_info->blocksize / new_blocksize;
	if (k==1) return 0;

	if (quiet)              
		memset(&progress, 0, sizeof(progress));
	else                    
		progress_init(&progress, _("reblock each inode: "),
				info->si_inodes);
	
	for (mpos=0; mpos < info->si_inodes; mpos++) {
		if ( !info->si_inode_table[mpos].i_links_count ) continue;

		progress_update(&progress, mpos);

		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			unsigned long frags_count = 0;
			struct frags_list *frags_list = NULL;
			struct frags_list **pfrags_list = &frags_list;

			unsigned long frag_offset = 0;

			bfl = 0;

			for ( frag=0; frag < info->si_inode_table[mpos].i_info.
					file_frags->fr_nfrags; frag++ )
			{
				unsigned long f;
				unsigned long nf;
				
				unsigned long *pfr_start = (unsigned long *) 
					&info->si_inode_table[mpos].i_info.
					file_frags->fr_frags[frag].fr_start;
				unsigned long *pfr_length = (unsigned long *) 
					&info->si_inode_table[mpos].i_info.
					file_frags->fr_frags[frag].fr_length;

				unsigned long fr_start = *pfr_start;
				unsigned long fr_length = *pfr_length;

				unsigned long *pnew_fr_start;
				unsigned long *pnew_fr_length;
				
				unsigned long block;

				if (k<1)
				{
					*pfr_start *= m;
					*pfr_length *= m;

					if ( (frag+1) == info->si_inode_table[mpos].i_info.
							file_frags->fr_nfrags )
					{
						unsigned long blocks = 
							(info->si_inode_table[mpos].i_size + 
							 new_blocksize - 1) / new_blocksize;
						*pfr_length = blocks - frag_offset;
					}

					frag_offset += *pfr_length;

					continue;
				}
				
				*pfrags_list = malloc (sizeof(struct frags_list));
				if (!(*pfrags_list)) return -ENOMEM;

				pnew_fr_start = (unsigned long *)
				       	&(*pfrags_list)->frag.fr_start;
				pnew_fr_length= (unsigned long *)
				       	&(*pfrags_list)->frag.fr_length;

				pfrags_list = &(*pfrags_list)->next;
				*pfrags_list = NULL;

				frags_count++;
				
				f =  min_t( unsigned long, k-bfl,
						fr_length );

				read_blocks (file, fr_start, f,
						buffer +
						bfl*blocks_info->blocksize, 
						blocks_info);
				bfl += f;

				*pnew_fr_start = fr_start/k;
				if ( test_block(*pnew_fr_start, 
							blocks_info, 
							new_blocksize) )
					(*pnew_fr_start)++;

				if (!fr_length)
				{
					*pnew_fr_length = 0;
					continue;
				}
				
				for (nf=0; f<fr_length; nf++)
				{
					read_blocks (file, (fr_start)?fr_start+f:0, 
							min_t( unsigned long, 2*k-bfl,
								fr_length - f ),
							buffer+
							bfl*blocks_info->blocksize, 
							blocks_info);
					
					bfl +=  min_t(unsigned long, 2*k-bfl,
							fr_length - f ) - k;
					f += min_t(unsigned long, 2*k-bfl, 
							fr_length - f);

					block = write_block (file, 
							(*pnew_fr_start)+nf,
							!fr_start,
							buffer, 
							blocks_info,
							new_blocksize);

					if (!block && fr_start) return -ENOSPC;
					if ( ( fr_start || (*pnew_fr_start) ) 
							?block!=( (*pnew_fr_start)+nf )
							:block )
					{
						*pnew_fr_length = nf;

						*pfrags_list = malloc (sizeof(struct frags_list));
						if (!(*pfrags_list)) return -ENOMEM;

						pnew_fr_start = (unsigned long *)
							&(*pfrags_list)->frag.fr_start;
						pnew_fr_length= (unsigned long *)
							&(*pfrags_list)->frag.fr_length;

						pfrags_list = &(*pfrags_list)->next;
						*pfrags_list = NULL;

						frags_count++;

						*pnew_fr_start = block;
						*pnew_fr_length = 0;
						nf = 0;
					}

					memcpy(buffer,
							buffer+
							k*blocks_info->blocksize,
							k*blocks_info->blocksize);
				}

				if ( (bfl==k) || ((frag+1) >= info->si_inode_table[mpos].i_info.
						file_frags->fr_nfrags) )
				{
					bfl = 0;
					block = write_block (file, 
							(*pnew_fr_start)+nf,
							!fr_start,
							buffer, 
							blocks_info,
							new_blocksize);

					if (!block && fr_start) return -ENOSPC;
					if ( ( fr_start || (*pnew_fr_start) ) 
							?block!=( (*pnew_fr_start)+nf )
							:block )
					{
						*pnew_fr_length = nf;

						*pfrags_list = malloc (sizeof(struct frags_list));
						if (!(*pfrags_list)) return -ENOMEM;

						pnew_fr_start = (unsigned long *)
							&(*pfrags_list)->frag.fr_start;
						pnew_fr_length= (unsigned long *)
							&(*pfrags_list)->frag.fr_length;

						pfrags_list = &(*pfrags_list)->next;
						*pfrags_list = NULL;

						frags_count++;

						*pnew_fr_start = block;
						*pnew_fr_length = 0;
						nf = 0;
					}

					nf++;
				}
				
				*pnew_fr_length = nf;
			}

			if (k<1)
			{
				//info->si_inode_table[mpos].i_info.
				//	file_frags->fr_nfrags*=m;
				continue;
			}

			if (verbose)
				printf (_("inode #%lu: +%lu fragments\n"), mpos,
						frags_count - info->si_inode_table[mpos].i_info.
						file_frags->fr_nfrags);

			info->si_inode_table[mpos].i_info.
				file_frags->fr_nfrags = frags_count;

			free ( info->si_inode_table[mpos].i_info.
					file_frags->fr_frags );

			info->si_inode_table[mpos].i_info.
				file_frags->fr_frags =
				malloc (frags_count*sizeof(struct any_file_fragment));

			if (!info->si_inode_table[mpos].i_info.
					file_frags->fr_frags)
				return -ENOMEM;
			
			{
				unsigned long f = 0;
				struct frags_list *frags_list_next;
				
				frags_list_next = frags_list->next;
				for (; frags_list; frags_list = frags_list_next)
				{
					if ( frags_count>1 && 
							!frags_list->frag.fr_length )
						info->si_inode_table[mpos].i_info.
							file_frags->fr_nfrags--;
					else
					{
						info->si_inode_table[mpos].i_info.
							file_frags->fr_frags[f] = 
							frags_list->frag;
						f++;
					}

					frags_list_next = frags_list->next;
					free(frags_list);
				}
			}
		}
	}
	progress_close(&progress);

	info->si_blocksize = new_blocksize;
	
	return r;
}

static void PRS(int argc, const char *argv[])
{
	int             c;
	int show_version_only = 0;
	char *          tmp;

	while ((c = getopt (argc, (char **) argv,
		     "nqvV")) != EOF) {
		switch (c) {
		case 'n':
			noaction++;
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
		default:
			usage();
		}
	}

	if ( ( optind >= (argc-2) ) && !show_version_only)
		usage();

	if (!quiet || show_version_only)
		fprintf (stderr, "reblock %s (%s)\n", ANYFSTOOLS_VERSION,
				ANYFSTOOLS_DATE);

	if (show_version_only) {
		exit(0);
	}
	
	inode_table = argv[optind++];
	device_name = argv[optind++];
	new_blocksize = strtol(argv[optind++], &tmp, 10);

	int log2 = 1;
	int b = new_blocksize;
	while ( (b = b>>1)>1 ) log2++;
	for (; log2; log2--) b = b<<1;
	if (b!=new_blocksize || new_blocksize<512 || *tmp) 
	{
		fprintf (stderr, 
	_("bad new blocksize. It is must be power of 2 and not less than 512\n"));
		exit(1);
	}
}

int main(int argc, const char *argv[])
{
	int r = 0;
	struct any_sb_info *info;
	int file;

	unsigned long bitmap_l;

	struct blocks_info blocks_info;

	struct stat64 stat_info;

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
#endif

	PRS(argc, argv);
	
	r = read_it (&info, (char*) inode_table);	
	if (r) 
	{
		fprintf(stderr, 
				_("Error while reading inode table: %s\n"),
				(errno)?strerror(errno):_("bad format"));
		goto out;
	}

	blocks_info.blocksize = info->si_blocksize;

	buffer = malloc(2*new_blocksize);
	if (!buffer) 
	{
		r = -ENOMEM;
		goto free_it_out;
	}

	file = open(device_name, (noaction ? O_RDONLY : O_RDWR) | O_LARGEFILE);
	if (file==-1) 
	{
		perror ("open");
		r = -errno;
		goto free_buf_out;
	}
	
	r = fstat64(file, &stat_info);
	if (r<0) {
		r = -errno;
		perror("lstat");
		goto close_out;
	}

	if (stat_info.st_mode & S_IFBLK)
	{
		if (ioctl(file, BLKGETSIZE, &stat_info.st_size) < 0) {
			perror("BLKGETSIZE");
			close(file);
			return -EIO;
		}
		stat_info.st_size*=512;
	}

	blocks_info.blocks_count = 
		stat_info.st_size/blocks_info.blocksize;

	bitmap_l = ( blocks_info.blocks_count +
			sizeof(unsigned long) - 1 )/sizeof(unsigned long);

	/*allocate memory for block bitmap*/
	blocks_info.block_bitmap =  (unsigned long*)
		malloc(bitmap_l*sizeof(unsigned long));
	if (!blocks_info.block_bitmap)
	{ r = -ENOMEM; goto close_out; }
	memset(blocks_info.block_bitmap, 0, bitmap_l*sizeof(unsigned long));

	r = fill_block_bitmap (info, blocks_info.block_bitmap, 
			blocks_info.blocks_count);
	if (r<0) goto free_bm1;

	set_bit(0, blocks_info.block_bitmap);

	r = reblock (info, file, &blocks_info, new_blocksize);
	if (r<0) 
	{
		switch (r)
		{
			case -ENOMEM:
				fprintf (stderr, _("Not enough memory\n"));
				break;

			case -ENOSPC:
				fprintf (stderr, _("Not enough space\n"));
				break;

			default:
				fprintf (stderr, _("Reblock return %d\n"), r);
				break;
		}
		r = -r;
		goto free_bm1;
	}
	
	if (!noaction)
	{
		r = write_it (info, (char*) inode_table);	
		if (r) 
		{
			fprintf(stderr, 
					_("Error while writing inode table: %s\n"),
					(errno)?strerror(errno):_("format error"));
			goto free_bm1;
		}
	}

free_bm1:
	free(blocks_info.block_bitmap);
	
close_out:
	close (file);

free_buf_out:
	free (buffer);
	
free_it_out:
	free_it (info);	

out:
	return r;
}
	
