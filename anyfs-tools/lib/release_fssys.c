/*
 * release_fssys.c
 * Copyright (C) 2006 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

/*
 * The idea of building filesystem is simple.
 * At the begining we save bitmap of blocks which will used by filesystem
 * and we know all blocks of information of old filesystem.
 * In general case new FS system blocks split block of info
 * (one info block -- is one fragment of a file) to 3 parts:
 *
 *        +---------+-----------------------+-------------+
 *        | head    |         body          |    tail     |
 *        +---------+-----------------------+-------------+
 * where, head -- is part of info block BEFORE new FS system block
 *     body -- is part of info which intersect with new FS system block
 *     tail -- is part of info block AFTER new FS system block
 *
 * Our task -- move "body" to new free space with adding MINIMUM of
 *     fragmentation of file.
 *
 * So we
 * At the first try to search free space for the whole fragment of file
 * and if it successful -- we move the whole fragment and fragmentation
 * don't increase.
 *
 * At the second try to search free space for head-body or body-tail part
 * of fragment. If it successful file fragmentation increase on 1 new fragment.
 *
 * At the third try to search free space for body part of fragment.
 * And then if it successful file fragmentation increase on 2 fragments.
 *
 * At the end if there are no free space block for the whole body
 * we split body on many small pieces and file fragmentation increase 
 * many fragments...
 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "any.h"
#include "bitops.h"

typedef int any_rwblk_t(unsigned long from, unsigned long n, char *buffer);
any_rwblk_t *any_readblk;
any_rwblk_t *any_writeblk;

typedef int any_testblk_t(unsigned long bitno);
any_testblk_t *any_testblk;

typedef unsigned long any_getblkcount_t();
any_getblkcount_t *any_getblkcount;

static unsigned long any_blocksize;

extern int noaction;
extern int verbose;
extern int quiet;

/* This function move _blocks_ group _from_ old offset _to_ new offset */
int any_move( unsigned long *block_bitmap,
		unsigned long from, unsigned long to,
		unsigned long blocks )
{
	unsigned long i;
	int retval;
	static char *buffer = NULL; 
	if (!buffer) 
	{
		buffer = malloc (any_blocksize);
		if (!buffer) return -ENOMEM;
	}
		
	for (i=0; i<blocks; i++) 
	{
		clear_bit ( from + i, block_bitmap );
		set_bit ( to + i, block_bitmap );
		
		if (!noaction)
		{
			retval = any_readblk(from + i, 1, buffer);
			if (retval) return -retval;
			retval = any_writeblk(to + i, 1, buffer);
			if (retval) return -retval;
		}
	}

	return 0;
}

struct frags_list {
	struct any_file_fragment frag;
	struct frags_list	 *next;
};

/* split body of fragment to many small pieces */
int any_break_body( struct any_sb_info *info,
	       	unsigned long *block_bitmap, 
		unsigned long ins_start, unsigned long ins_end,
		unsigned long inode, unsigned long frag)
{
	unsigned long start, length;
	unsigned long i;
	unsigned long l, t;
	int retval;
	
	struct any_file_frags * file_frags = 
		info->si_inode_table[inode].i_info.file_frags;
	struct any_file_fragment *fr_frags;

	struct frags_list *frags_list = NULL;
	struct frags_list **pfrags_list = &frags_list;
	unsigned long nfrags = 0;
	
	l = 0;
	t = ins_end - ins_start + 1;

	start = file_frags->fr_frags[frag].fr_start;
	length= ins_start - file_frags->fr_frags[frag].fr_start;
	
	if (length) {
		*pfrags_list = malloc (sizeof(struct frags_list));
		if (!(*pfrags_list)) return -ENOMEM;

		(*pfrags_list)->frag.fr_start = start;
		(*pfrags_list)->frag.fr_length= length;

		pfrags_list = &(*pfrags_list)->next;
		*pfrags_list = NULL;

		nfrags++;
	}
	
	start = length = 0;
	
	for (i=1; i<any_getblkcount(); i++) {
		if ( ! ( test_bit ( i, block_bitmap ) || 
				any_testblk(i) ) ) {

			if (!length) start = i;
				
			if (i!=(start+length)) {
				retval = any_move(block_bitmap, ins_start + l, start, 
						min_t(unsigned long, length, t-l));
				if (retval<0) return retval;

				*pfrags_list = malloc (sizeof(struct frags_list));
				if (!(*pfrags_list)) return -ENOMEM;

				(*pfrags_list)->frag.fr_start = start;
				(*pfrags_list)->frag.fr_length= 
					min_t(unsigned long, length, t-l);
				
				pfrags_list = &(*pfrags_list)->next;
				*pfrags_list = NULL;

				nfrags++;

				l += min_t(unsigned long, length, t-l);

				if (l>=t) goto out;
				
				start = i;
				length = 0;
			}

			length++;
		}
	}
	
	retval = any_move(block_bitmap, ins_start + l, start, 
			min_t(unsigned long, length, t-l));
	if (retval<0) return retval;

	*pfrags_list = malloc (sizeof(struct frags_list));
	if (!(*pfrags_list)) return -ENOMEM;

	(*pfrags_list)->frag.fr_start = start;
	(*pfrags_list)->frag.fr_length= 
		min_t(unsigned long, length, t-l);

	pfrags_list = &(*pfrags_list)->next;
	*pfrags_list = NULL;

	nfrags++;

	l += min_t(unsigned long, length, t-l);

out:
	start = ins_end + 1;
	length= (file_frags->fr_frags[frag].fr_start +
			file_frags->fr_frags[frag].fr_length -1) - ins_end;

	if (length)
	{
		*pfrags_list = malloc (sizeof(struct frags_list));
		if (!(*pfrags_list)) return -ENOMEM;

		(*pfrags_list)->frag.fr_start = start;
		(*pfrags_list)->frag.fr_length= length;

		pfrags_list = &(*pfrags_list)->next;
		*pfrags_list = NULL;

		nfrags++;
	}
	
	fr_frags = malloc((file_frags->fr_nfrags + nfrags-1)*
			sizeof(struct any_file_fragment));
	if (!fr_frags) return -ENOMEM;

	memcpy(fr_frags, file_frags->fr_frags,
			frag*sizeof(struct any_file_fragment));
	memcpy(fr_frags + (frag+nfrags), 
			file_frags->fr_frags + (frag+1),
			(file_frags->fr_nfrags-frag-1)*
			sizeof(struct any_file_fragment) );

	file_frags->fr_nfrags += nfrags-1;

	free(file_frags->fr_frags);
	file_frags->fr_frags = fr_frags;

	for (i=0; i<nfrags; i++)
	{
		struct frags_list *nextfrag;
		
		fr_frags[frag+i] = frags_list->frag;
		
		nextfrag = frags_list->next;
		free (frags_list);
		frags_list = nextfrag;
	}

	printf (_("+%lu fragments\n"), nfrags-1);

	if (l>=t) return 0;

	printf (_("Sorry, I can't find yet %lu free blocks\n"), t-l);
	return -ENOSPC;
}

/* move body of fragment */
int any_body_move( struct any_sb_info *info,
	       	unsigned long *block_bitmap, 
		unsigned long ins_start, unsigned long ins_end,
		unsigned long start_blk, 
		unsigned long inode, unsigned long frag)
{
	int retval;
	struct any_file_frags * file_frags = 
		info->si_inode_table[inode].i_info.file_frags;
	struct any_file_fragment *fr_frags;

	file_frags->fr_nfrags+=2;
	
	fr_frags = malloc(file_frags->fr_nfrags*
			sizeof(struct any_file_fragment));
	if (!fr_frags) return -ENOMEM;

	memcpy(fr_frags, file_frags->fr_frags,
			(frag+1)*sizeof(struct any_file_fragment));
	memcpy(fr_frags + (frag+3), 
			file_frags->fr_frags + (frag+1),
			(file_frags->fr_nfrags-frag-3)
			*sizeof(struct any_file_fragment) );

	free(file_frags->fr_frags);
	file_frags->fr_frags = fr_frags;
	
	retval = any_move(block_bitmap, ins_start, start_blk, ins_end-ins_start+1);
	if (retval<0) return retval;

	fr_frags[frag+1].fr_start = start_blk;
	fr_frags[frag+1].fr_length = ins_end-ins_start+1;

	fr_frags[frag+2].fr_start = ins_end+1;
	fr_frags[frag+2].fr_length = fr_frags[frag].fr_start + 
		fr_frags[frag].fr_length - ins_end-1;
	
	fr_frags[frag].fr_length= ins_start - fr_frags[frag].fr_start;

	return 0;
}

/* move body and head or tail (hot) of fragment */
int any_bodyhot_move(struct any_sb_info *info,
	       	unsigned long *block_bitmap, 
		unsigned long fr_start, unsigned long fr_length,
		unsigned long ins_start, unsigned long ins_end,
		unsigned long start_blk, unsigned long blocks, int ht, 
		unsigned long inode, unsigned long frag)
{
	int retval;
	struct any_file_frags * file_frags = 
		info->si_inode_table[inode].i_info.file_frags;
	struct any_file_fragment *fr_frags;

	file_frags->fr_nfrags++;
	
	fr_frags = malloc(file_frags->fr_nfrags*
			sizeof(struct any_file_fragment));
	if (!fr_frags) return -ENOMEM;

	memcpy(fr_frags, file_frags->fr_frags,
			(frag+1)*sizeof(struct any_file_fragment));
	memcpy(fr_frags + (frag+2), 
			file_frags->fr_frags + (frag+1),
			(file_frags->fr_nfrags-frag-2)*
			sizeof(struct any_file_fragment) );

	free(file_frags->fr_frags);
	file_frags->fr_frags = fr_frags;

	if (ht) {
		retval = any_move(block_bitmap, 
				fr_start, start_blk, ins_end-fr_start+1);
		if (retval<0) return retval;

		fr_frags[frag+1].fr_start =
			fr_start + ins_end-fr_start+1;
		fr_frags[frag+1].fr_length=
			fr_frags[frag].fr_length - (ins_end-fr_start+1);
		
		fr_frags[frag].fr_start = start_blk;
		fr_frags[frag].fr_length = ins_end-fr_start+1;
	}
	else {
		retval = any_move(block_bitmap, ins_start, start_blk, 
				(fr_start+fr_length-1) - ins_start + 1);
		if (retval<0) return retval;

		fr_frags[frag].fr_length -=
			(fr_start+fr_length-1) - ins_start + 1;
		
		fr_frags[frag+1].fr_start = start_blk;
		fr_frags[frag+1].fr_length= 
			(fr_start+fr_length-1) - ins_start + 1;
	}
	
	return 0;
}

/* move the whole fragment of file */
int any_whole_move(struct any_sb_info *info,
	       	unsigned long *block_bitmap,       
		unsigned long fr_start, unsigned long fr_length,
		unsigned long start_blk,
		unsigned long inode, unsigned long frag)
{
	int retval;
	struct any_file_frags * file_frags = 
		info->si_inode_table[inode].i_info.file_frags;

	retval = any_move(block_bitmap, fr_start, start_blk, fr_length);
	if (retval<0) return retval;

	file_frags->fr_frags[frag].fr_start = start_blk;

	return 0;
}

/* 
 * try search free space block of size size1, size2 or size3.
 * size1 <= size2 <= size3.
 * The greater free space block is prefer.
 */
int any_find_frees(unsigned long *block_bitmap,
		unsigned long size1, unsigned long size2, unsigned long size3,
		unsigned long *start_blk, unsigned long *blocks)
{
	unsigned long start = 0;
	unsigned long length = 0;
	unsigned long i;

	int r=0;
	unsigned long start_blk1=0, blocks1=0;
	unsigned long start_blk2=0, blocks2=0;

	for (i=1; i<any_getblkcount(); i++) {
		if ( ! ( test_bit ( i, block_bitmap ) || 
				any_testblk(i) ) ) {
			
			if (!length) start = i;
				
			if (i!=(start+length)) {
				if (length >= size3) {
					*start_blk = start;
					*blocks = length;
					return 3;
				} else if (length >= size2) {
					if (r<2) {
						start_blk2 = start;
						blocks2 = length;
						r = 2;
					}
				} else if (length >= size1) {
					if (r<1) {
						start_blk1 = start;
						blocks1 = length;
						r = 1;
					}
				}
				
				start = i;
				length = 0;
			}
			length++;
		}
	}

	if (length >= size3) {
		*start_blk = start;
		*blocks = length;
		return 3;
	} else if (length >= size2) {
		if (r<2) {
			start_blk2 = start;
			blocks2 = length;
			r = 2;
		}
	} else if (length >= size1) {
		if (r<1) {
			start_blk1 = start;
			blocks1 = length;
			r = 1;
		}
	}

	switch (r) {
		case 1:
			*start_blk = start_blk1;
			*blocks = blocks1;
			break;
		case 2:
			*start_blk = start_blk2;
			*blocks = blocks2;
			break;
	}

	return r;
}

/*release space for system information of FS*/
int any_release(struct any_sb_info *info, 
		unsigned long *block_bitmap,
		unsigned long start, unsigned long length)
{
	unsigned long i;
	int ret;

	any_blocksize = info->si_blocksize;
	
	for (i=0; i<length; i++) {
		if ( test_bit ( start + i, block_bitmap ) )
		{
			unsigned long mpos;
			unsigned long frag;
			if (verbose>=3)
				printf (_("data_block intersect with a file(s)\n"));
			for (mpos=0; mpos < info->si_inodes; mpos++) {
				if ( !info->si_inode_table[mpos].i_links_count ) continue;

				if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
					for ( frag=0; frag < info->si_inode_table[mpos].i_info.
							file_frags->fr_nfrags; frag++ )
					{
						unsigned long fr_start = info->si_inode_table[mpos].i_info.
								file_frags->fr_frags[frag].fr_start;
						unsigned long fr_length = info->si_inode_table[mpos].i_info.
								file_frags->fr_frags[frag].fr_length;
						/*skip sparse blocks*/
						if (!fr_start) continue;

						long long ins_start, ins_end;
						
						ins_start = max_t(unsigned long, fr_start, start);
						ins_end = min_t(unsigned long, 
								fr_start + fr_length - 1,
								start + length - 1);

						if (ins_start <= ins_end) {
							unsigned long head, body, tail;
							unsigned long size1, size2, size3;
							int ht, r;
							unsigned long start_blk, blocks;
							
							if (verbose>=3)
								printf (_("inode #%lu, fragment #%lu/%lu (from %lu to %lu)\n"
										"	by blocks from %lld to %lld\n"),
										mpos, frag,
										(long unsigned) info->si_inode_table[mpos].i_info.
										file_frags->fr_nfrags,
									       	fr_start, fr_start+fr_length-1,
										ins_start, ins_end);

							head = ins_start - fr_start;
							body = ins_end - ins_start + 1;
							tail = (fr_start+fr_length-1) - ins_end;
							ht = (head<tail);

							size1 = body;
							size2 = (ht) ? body + head : body + tail;
							size3 = head + body + tail;
							
							r = any_find_frees(block_bitmap,
									size1, size2, size3,
									&start_blk, &blocks);

							switch (r) {
								case 0:
									if (verbose>=3)
										printf (_("Will breaks on many fragments\n"));
									ret = any_break_body( info, block_bitmap, 
											ins_start, ins_end, mpos, frag );
									if (ret<0) return ret;
									break;
								case 1:
									if (verbose>=3)
										printf (_("Body will move to free block from %lu to %lu\n"),
												start_blk, start_blk+blocks-1);
									ret = any_body_move( info, block_bitmap, 
											ins_start, ins_end,
											start_blk, mpos, frag );
									if (ret<0) return ret;
									break;
								case 2:
									if (verbose>=3)
										printf (_("Body with %s will move to free block from %lu to %lu\n"),
												(ht)?_("head"):_("tail"), start_blk, start_blk+blocks-1);
									ret = any_bodyhot_move( info, block_bitmap, 
											fr_start, fr_length,
											ins_start, ins_end,
											start_blk, blocks, ht, mpos, frag);
									if (ret<0) return ret;
									break;
								case 3:
									if (verbose>=3)
										printf (_("Whole fragment will move to free block from %lu to %lu\n"),
												start_blk, start_blk+blocks-1);
									ret = any_whole_move( info, block_bitmap,       
											fr_start, fr_length,
											start_blk, mpos, frag);
									if (ret<0) return ret;
									break;
							}
						}
					}
				}
			}
			return 0;
		}
	}
	
	return 0;
}

#include <progress.h>
#include <super.h>

int any_release_sysinfo(struct any_sb_info *info, 
		unsigned long *block_bitmap,
		any_rwblk_t *readblk,
		any_rwblk_t *writeblk,
		any_testblk_t *testblk,
		any_getblkcount_t *getblkcount)
{
	int retval;
	uint32_t i;
	struct progress_struct progress;
	unsigned long start = 0;
	unsigned long length = 0;

	if (verbose)
		printf(_("Starting search of info blocks at system blocks\n"));

	any_readblk = readblk;
	any_writeblk = writeblk;
	any_testblk = testblk;
	any_getblkcount = getblkcount;

	if (quiet)
		memset(&progress, 0, sizeof(progress));
	else
		progress_init(&progress, _("Search user info at system blocks: "),
				getblkcount());

	for (i=0; i<getblkcount(); i++) {
		progress_update(&progress, i);
		if ( testblk(i) ) {
			if (i!=(start+length)) {
				if (verbose>=2)
					printf (_("\nRelease blocks from %lu to %lu\n"), start,
							start+length-1);
				retval = any_release(info, block_bitmap,
						start, length);
				if (retval<0) 
				{
					if (!noaction)
						write_it (info, NULL);
					goto out;
				}

				start = i;
				length = 0;
			}
			length++;
		}
	}

	if (verbose>=2)
		printf (_("\nRelease blocks from %lu to %lu\n"), start,
				start+length-1);
	retval = any_release(info, block_bitmap,
			start, length);
	if (retval<0) goto out;

	if (!noaction) 
	{
		retval = write_it (info, NULL);
		if (retval) 
		{
			fprintf(stderr,
					_("Error while writing inode table: %s\n"),
					(errno)?strerror(errno):_("format error"));
			exit(retval);
		}
	}

out:	
	progress_close(&progress);

	return retval;
}

#include <string.h>

/*add dot (".") and dotdot ("..") entries to directories*/
int any_adddadd(struct any_sb_info *info)
{
	unsigned long i;
	unsigned long mpos = 1;

	struct any_dirent* new_dirent;
	info->si_inode_table[mpos].i_info.dir->d_ndirents+=2;

	new_dirent = malloc(sizeof(struct any_dirent));
	if (!new_dirent) return -ENOMEM;
	new_dirent->d_name = malloc(strlen("..")+1);
	if (!new_dirent->d_name) return -ENOMEM;
	strcpy(new_dirent->d_name, "..");
	new_dirent->d_inode = mpos;
	new_dirent->d_next = info->si_inode_table[mpos].i_info.dir->d_dirent;
	info->si_inode_table[mpos].i_info.dir->d_dirent = new_dirent;

	new_dirent = malloc(sizeof(struct any_dirent));
	if (!new_dirent) return -ENOMEM;
	new_dirent->d_name = malloc(strlen(".")+1);
	if (!new_dirent->d_name) return -ENOMEM;
	strcpy(new_dirent->d_name, ".");
	new_dirent->d_inode = mpos;
	new_dirent->d_next = info->si_inode_table[mpos].i_info.dir->d_dirent;
	info->si_inode_table[mpos].i_info.dir->d_dirent = new_dirent;

	for (i=0; i<info->si_inodes; i++) {
		if ( !info->si_inode_table[i].i_links_count ) continue;

		if ( S_ISDIR(info->si_inode_table[i].i_mode) ) {
			struct any_dirent       *dirent;

			dirent = info->si_inode_table[i].i_info.
				dir->d_dirent;
			for (; dirent; dirent = dirent->d_next) {
				unsigned long mpos;

				if (strcmp(dirent->d_name, ".")==0) continue;
				if (strcmp(dirent->d_name, "..")==0) continue;

				mpos = dirent->d_inode;
				if ( S_ISDIR(info->si_inode_table[mpos].i_mode) ) {
					struct any_dirent* new_dirent;
					info->si_inode_table[mpos].i_info.dir->d_ndirents+=2;

					new_dirent = malloc(sizeof(struct any_dirent));
					if (!new_dirent) return -ENOMEM;
					new_dirent->d_name = malloc(strlen("..")+1);
					if (!new_dirent->d_name) return -ENOMEM;
					strcpy(new_dirent->d_name, "..");
					new_dirent->d_inode = i;
					new_dirent->d_next = info->si_inode_table[mpos].i_info.dir->d_dirent;
					info->si_inode_table[mpos].i_info.dir->d_dirent = new_dirent;

					new_dirent = malloc(sizeof(struct any_dirent));
					if (!new_dirent) return -ENOMEM;
					new_dirent->d_name = malloc(strlen(".")+1);
					if (!new_dirent->d_name) return -ENOMEM;
					strcpy(new_dirent->d_name, ".");
					new_dirent->d_inode = mpos;
					new_dirent->d_next = info->si_inode_table[mpos].i_info.dir->d_dirent;
					info->si_inode_table[mpos].i_info.dir->d_dirent = new_dirent;
				}
			}
		}
	}

	return 0;
}

