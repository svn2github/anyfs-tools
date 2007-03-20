/*
 *	filesystem_info_descr.c
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

#include <dirent.h>
#include "anysurrect.h"
#include "direct_io.h"
#include "filesystem_info_descr.h"

/*ext2fs direct blocks links*/
#include "any.h"

char *filesystem_info_ext2fs_direct_blocks_links_surrect()
{
	any_size_t to_offset = get_blocksize();
	uint32_t last_block = READ_LELONG("block_link");
	uint32_t frags = 1;
	uint32_t blocks = 1;
	uint32_t non_zero_blocks = last_block?1:0;
	uint32_t zeroes_at_end = last_block?0:1;
	uint32_t tz = 0;
	if ( last_block > device_blocks ) return NULL;
	
	while ( fd_seek(0, SEEK_CUR) < to_offset )
	{
		uint32_t block = READ_LELONG("block_link");
		if ( block > device_blocks ) return NULL;
		if ( ( !last_block && block ) ||
				( block && block!=(last_block+1) ) )
			frags++;

		if (!block) zeroes_at_end++;
		else 
		{
			if (zeroes_at_end) tz++;
			zeroes_at_end=0;
		}

		last_block = block;
		if (block) 
			non_zero_blocks++;
		blocks++;
	}

	if (tz>2) return NULL;
	if (zeroes_at_end==blocks) return NULL;
	if ( frags > (blocks/10) ) return NULL;
	if ( frags>(non_zero_blocks/4) ) return NULL;

	if ( frags == 1 )
	{
		fd_seek(to_offset, SEEK_SET);
		return "filesystem_info/ext2fs/direct_blocks_links";
	}

	struct frags_list *frags_list = NULL;
	struct frags_list *cur_frag = NULL;
	fd_seek(0, SEEK_SET);

	uint32_t block = READ_LELONG("block_link");
	int i;
	int r = 12;
	if ( (block - 1) == get_block() ) r++;
	for (i=0; i<12; i++)
		cur_frag = addblock_to_frags_list(&frags_list, cur_frag, block - r + i);
	cur_frag = addblock_to_frags_list(&frags_list, cur_frag, block);

	while ( fd_seek(0, SEEK_CUR) < to_offset )
	{
		uint32_t block = READ_LELONG("block_link");
		cur_frag = addblock_to_frags_list(&frags_list, cur_frag, block);
	}

	anysurrect_frags_list(frags_list, 0, "filesystem_files/ext2fs/direct_blocks_links");

	fd_seek(to_offset, SEEK_SET);

	return "filesystem_info/ext2fs/direct_blocks_links";
}

char *filesystem_info_ext2fs_direct_blocks_links_surrect_dr()
{
	any_size_t to_offset = get_blocksize();
	uint32_t last_block = READ_LELONG_DR("block_link");
	uint32_t frags = 1;
	uint32_t blocks = 1;
	uint32_t non_zero_blocks = last_block?1:0;
	uint32_t zeroes_at_end = last_block?0:1;
	uint32_t tz = 0;
	if ( last_block > device_blocks ) return NULL;
	
	while ( fd_seek_dr(0, SEEK_CUR) < to_offset )
	{
		uint32_t block = READ_LELONG_DR("block_link");
		if ( block > device_blocks ) return NULL;
		if ( ( !last_block && block ) ||
				( block && block!=(last_block+1) ) )
			frags++;

		if (!block) zeroes_at_end++;
		else 
		{
			if (zeroes_at_end) tz++;
			zeroes_at_end=0;
		}

		last_block = block;
		if (block) 
			non_zero_blocks++;
		blocks++;
	}

	if (tz>2) return NULL;
	if (zeroes_at_end==blocks) return NULL;
	if ( frags > (blocks/10) ) return NULL;
	if ( frags>(non_zero_blocks/4) ) return NULL;

	return "filesystem_info/ext2fs/direct_blocks_links";
}

struct frags_list *filesystem_info_ext2fs_direct_blocks_links_surrect_dr_to_frags_list(
		struct frags_list **pfrags_list_begin, 
		struct frags_list *pfrags_list)
{
	any_size_t to_offset = get_blocksize();
	while ( fd_seek_dr(0, SEEK_CUR) < to_offset )
	{
		uint32_t block = READ_LELONG_DR("block_link");
		pfrags_list = addblock_to_frags_list(pfrags_list_begin, pfrags_list, block);
	}

	return pfrags_list;
}

int test_direct_links(uint32_t block)
{
	int res = 0;
	any_off_t old_offset = fd_seek_dr(0, SEEK_CUR);
	any_off_t old_direct_start = fd_get_direct_start();
	
	fd_set_direct_start(block*get_blocksize());
	fd_seek_dr(0, SEEK_SET);
	
	if ( filesystem_info_ext2fs_direct_blocks_links_surrect_dr() )
		res = 1;

	fd_set_direct_start(old_direct_start);
	fd_seek_dr(old_offset, SEEK_SET);
	return res;
}

struct frags_list *direct_links_to_frags_list(
		struct frags_list **pfrags_list_begin, 
		struct frags_list *pfrags_list,
		uint32_t block)
{
	struct frags_list *res = NULL;
	any_off_t old_offset = fd_seek_dr(0, SEEK_CUR);
	any_off_t old_direct_start = fd_get_direct_start();
	
	fd_set_direct_start(block*get_blocksize());
	fd_seek_dr(0, SEEK_SET);
	
	res = filesystem_info_ext2fs_direct_blocks_links_surrect_dr_to_frags_list(
			pfrags_list_begin, pfrags_list);

	fd_set_direct_start(old_direct_start);
	fd_seek_dr(old_offset, SEEK_SET);
	return res;
}

char *filesystem_info_ext2fs_indirect_blocks_links_surrect()
{
	any_size_t to_offset = get_blocksize();
	int only_zero_blocks = 0;
	int first = 1;
	
	while ( fd_seek(0, SEEK_CUR) < to_offset )
	{
		uint32_t block = READ_LELONG("block_link");

		if (only_zero_blocks)
		{
			if (block) return NULL;
			continue;
		}
		
		if (!block)
		{
			if (first) return NULL;
			only_zero_blocks = 1;
			continue;
		}

		first = 0;
		
		if ( block > device_blocks ) return NULL;
		if ( !test_direct_links(block) ) return NULL;
	}

	struct frags_list *frags_list = NULL;
	struct frags_list *cur_frag = NULL;
	fd_seek(0, SEEK_SET);

	uint32_t block = READ_LELONG("block_link");
	int i;
	int r1 = 12;
	int r2 = 1024;
	if ( (block - 1) == get_block() ) 
	{ r1++; r2++; }
	for (i=0; i<12; i++)
		cur_frag = addblock_to_frags_list(&frags_list, cur_frag, block - r1 - r2 + i);
	for (i=0; i<1024; i++)
		cur_frag = addblock_to_frags_list(&frags_list, cur_frag, block - r2 + i);
	cur_frag = direct_links_to_frags_list(&frags_list, cur_frag, block);

	while ( block )
	{
		block = READ_LELONG("block_link");
		cur_frag = direct_links_to_frags_list(&frags_list, cur_frag, block);
	}

	{
#define NEXT_FRAG_WA_OFS(frag, offnext)                 \
		        (void*)( (offnext) ? (char*)frag + offnext : 0 )

#define NEXT_FRAG_OFS(frag, offnext)            \
		        ( frag = NEXT_FRAG_WA_OFS(frag, offnext) )

#define NEXT_FRAG(frag) NEXT_FRAG_OFS(frag, frag->offnext)
#define NEXT_FRAG_WA(frag) NEXT_FRAG_WA_OFS(frag, frag->offnext)

		struct frags_list *p = frags_list;
		if (!p) goto out;
		NEXT_FRAG(p);
		if (!p) goto out;
		if ( (block - 1) == get_block() ) 
		{
			NEXT_FRAG(p);
			if (!p) goto out;
			NEXT_FRAG(p);
			if (!p) goto out;
		}
	}

	anysurrect_frags_list(frags_list, 0, "filesystem_files/ext2fs/indirect_blocks_links");

out:
	fd_seek(to_offset, SEEK_SET);

	return "filesystem_info/ext2fs/indirect_blocks_links";
}

char *filesystem_info_ext2fs_indirect_blocks_links_surrect_dr()
{
	any_size_t to_offset = get_blocksize();
	int only_zero_blocks = 0;
	int first = 1;
	
	while ( fd_seek_dr(0, SEEK_CUR) < to_offset )
	{
		uint32_t block = READ_LELONG_DR("block_link");

		if (only_zero_blocks)
		{
			if (block) return NULL;
			continue;
		}
		
		if (!block)
		{
			if (first) return NULL;
			only_zero_blocks = 1;
			continue;
		}

		first = 0;
		
		if ( block > device_blocks ) return NULL;
		if ( !test_direct_links(block) ) return NULL;
	}

	return "filesystem_info/ext2fs/indirect_blocks_links";
}

struct frags_list *filesystem_info_ext2fs_indirect_blocks_links_surrect_dr_to_frags_list(
		struct frags_list **pfrags_list_begin, 
		struct frags_list *pfrags_list)
{
	any_size_t to_offset = get_blocksize();
	uint32_t block;
	do
	{
		block = READ_LELONG_DR("block_link");
		pfrags_list = direct_links_to_frags_list(pfrags_list_begin, pfrags_list, block);
	} while ( fd_seek_dr(0, SEEK_CUR) < to_offset && block );

	return pfrags_list;
}


int test_indirect_links(uint32_t block)
{
	int res = 0;
	any_off_t old_offset = fd_seek_dr(0, SEEK_CUR);
	any_off_t old_direct_start = fd_get_direct_start();
	
	fd_set_direct_start(block*get_blocksize());
	fd_seek_dr(0, SEEK_SET);
	
	if ( filesystem_info_ext2fs_indirect_blocks_links_surrect_dr() )
		res = 1;

	fd_set_direct_start(old_direct_start);
	fd_seek_dr(old_offset, SEEK_SET);
	return res;
}

struct frags_list *indirect_links_to_frags_list(
		struct frags_list **pfrags_list_begin, 
		struct frags_list *pfrags_list,
		uint32_t block)
{
	struct frags_list *res = NULL;
	any_off_t old_offset = fd_seek_dr(0, SEEK_CUR);
	any_off_t old_direct_start = fd_get_direct_start();
	
	fd_set_direct_start(block*get_blocksize());
	fd_seek_dr(0, SEEK_SET);
	
	res = filesystem_info_ext2fs_indirect_blocks_links_surrect_dr_to_frags_list(
			pfrags_list_begin, pfrags_list);

	fd_set_direct_start(old_direct_start);
	fd_seek_dr(old_offset, SEEK_SET);
	return res;
}

char *filesystem_info_ext2fs_double_indirect_blocks_links_surrect()
{
	any_size_t to_offset = get_blocksize();
	int only_zero_blocks = 0;
	int first = 1;
	
	while ( fd_seek(0, SEEK_CUR) < to_offset )
	{
		uint32_t block = READ_LELONG("block_link");

		if (only_zero_blocks)
		{
			if (block) return NULL;
			continue;
		}
		
		if (!block)
		{
			if (first) return NULL;
			only_zero_blocks = 1;
			continue;
		}

		first = 0;
		
		if ( block > device_blocks ) return NULL;
		if ( !test_indirect_links(block) ) return NULL;
	}

	return "filesystem_info/ext2fs/double_indirect_blocks_links";
}

char *filesystem_info_ext2fs_double_indirect_blocks_links_surrect_dr()
{
	any_size_t to_offset = get_blocksize();
	int only_zero_blocks = 0;
	int first = 1;
	
	while ( fd_seek_dr(0, SEEK_CUR) < to_offset )
	{
		uint32_t block = READ_LELONG_DR("block_link");

		if (only_zero_blocks)
		{
			if (block) return NULL;
			continue;
		}
		
		if (!block)
		{
			if (first) return NULL;
			only_zero_blocks = 1;
			continue;
		}

		first = 0;
		
		if ( block > device_blocks ) return NULL;
		if ( !test_indirect_links(block) ) return NULL;
	}

	return "filesystem_info/ext2fs/double_indirect_blocks_links";
}

struct frags_list *filesystem_info_ext2fs_double_indirect_blocks_links_surrect_dr_to_frags_list(
		struct frags_list **pfrags_list_begin, 
		struct frags_list *pfrags_list)
{
	any_size_t to_offset = get_blocksize();
	uint32_t block;
	do
	{
		block = READ_LELONG_DR("block_link");
		pfrags_list = indirect_links_to_frags_list(pfrags_list_begin, pfrags_list, block);
	} while ( fd_seek_dr(0, SEEK_CUR) < to_offset && block );

	return pfrags_list;
}


int test_double_indirect_links(uint32_t block)
{
	int res = 0;
	any_off_t old_offset = fd_seek_dr(0, SEEK_CUR);
	any_off_t old_direct_start = fd_get_direct_start();
	
	fd_set_direct_start(block*get_blocksize());
	fd_seek_dr(0, SEEK_SET);
	
	if ( filesystem_info_ext2fs_double_indirect_blocks_links_surrect_dr() )
		res = 1;

	fd_set_direct_start(old_direct_start);
	fd_seek_dr(old_offset, SEEK_SET);
	return res;
}

struct frags_list *double_indirect_links_to_frags_list(
		struct frags_list **pfrags_list_begin, 
		struct frags_list *pfrags_list,
		uint32_t block)
{
	struct frags_list *res = NULL;
	any_off_t old_offset = fd_seek_dr(0, SEEK_CUR);
	any_off_t old_direct_start = fd_get_direct_start();
	
	fd_set_direct_start(block*get_blocksize());
	fd_seek_dr(0, SEEK_SET);
	
	res = filesystem_info_ext2fs_double_indirect_blocks_links_surrect_dr_to_frags_list(
			pfrags_list_begin, pfrags_list);

	fd_set_direct_start(old_direct_start);
	fd_seek_dr(old_offset, SEEK_SET);
	return res;
}

char *filesystem_info_ext2fs_inode_table_surrect()
{
	any_size_t to_offset = get_blocksize();
	int l1 = 0;
	int l2 = 0;
	
	while ( fd_seek(0, SEEK_CUR) < to_offset )
	{
		uint16_t mode = READ_LESHORT("mode");
		SKIP_LESHORT("uid");
		uint32_t size = READ_LELONG("size");
		uint32_t atime = READ_LELONG("atime");
		uint32_t ctime = READ_LELONG("ctime");
		uint32_t mtime = READ_LELONG("mtime");
		uint32_t dtime = READ_LELONG("dtime");
		SKIP_LESHORT("gid");
		uint16_t links = READ_LESHORT("links_count");
		SKIP_LELONG("blocks");
		SKIP_LELONG("flags");
		SKIP_LELONG("reserved1");
		uint32_t blocks[12];
		uint32_t direct_links_block;
		uint32_t indirect_links_block;
		uint32_t double_indirect_links_block;
		int i;
		for (i=0; i<12; i++)
			blocks[i] = COND_LELONG("direct_link", val <= device_blocks);
		direct_links_block = COND_LELONG("indirect_link", val <= device_blocks);
		indirect_links_block = COND_LELONG("double_indirect_link", val <= device_blocks);
		double_indirect_links_block = COND_LELONG("3x-indirect_link", val <= device_blocks);
		SKIP_LELONG("version");
		SKIP_LELONG("file_acl");
		uint32_t size_high = READ_LELONG("dir_acl OR size_high");
		SKIP_LELONG("faddr");
		SKIP_BYTE("frag");
		SKIP_BYTE("fsize");
		SKIP_LESHORT("pad1");
		SKIP_STRING("reserved2", 4*2);

		if ( links && (ctime>atime || ctime>mtime) ) return NULL;
		if (links && dtime) return NULL;
		if (!links && dtime && ctime>dtime) return NULL;

		uint64_t size64 = size;
		if ( (mode>>12) == DT_REG )
			size64 = (uint64_t) size | (uint64_t) size_high<<32;
		if ( links && !(mode<<12) ) return NULL;

		if (links) l1 = 1;
		if ( (mode>>12) == DT_REG ) l2 = 1;
	}
	if (!l1 || !l2) return NULL;

	fd_seek(0, SEEK_SET);

	while ( fd_seek(0, SEEK_CUR) < to_offset )
	{
		uint16_t mode = READ_LESHORT("mode");
		SKIP_LESHORT("uid");
		uint32_t size = READ_LELONG("size");
		SKIP_LELONG("atime");
		SKIP_LELONG("ctime");
		SKIP_LELONG("mtime");
		SKIP_LELONG("dtime");
		SKIP_LESHORT("gid");
		uint16_t links = READ_LESHORT("links_count");
		SKIP_LELONG("blocks");
		SKIP_LELONG("flags");
		SKIP_LELONG("reserved1");
		uint32_t blocks[12];
		uint32_t direct_links_block;
		uint32_t indirect_links_block;
		uint32_t double_indirect_links_block;
		int i;
		for (i=0; i<12; i++)
			blocks[i] = READ_LELONG("direct_link");
		direct_links_block = READ_LELONG("indirect_link");
		indirect_links_block = READ_LELONG("double_indirect_link");
		double_indirect_links_block = READ_LELONG("3x-indirect_link");
		SKIP_LELONG("version");
		SKIP_LELONG("file_acl");
		uint32_t size_high = READ_LELONG("dir_acl OR size_high");
		SKIP_LELONG("faddr");
		SKIP_BYTE("frag");
		SKIP_BYTE("fsize");
		SKIP_LESHORT("pad1");
		SKIP_STRING("reserved2",4*2);

		any_size_t seek = fd_seek(0, SEEK_CUR);

		if ( (mode>>12) == DT_REG && links )
		{
			uint64_t size64;
			size64 = (uint64_t) size | (uint64_t) size_high<<32;

			struct frags_list *frags_list = NULL;
			struct frags_list *cur_frag = NULL;
			int i;
			for (i=0; i<12; i++)
				cur_frag = addblock_to_frags_list(&frags_list, cur_frag, blocks[i]);
			
			if (direct_links_block)
			{
				cur_frag = direct_links_to_frags_list(&frags_list, 
						cur_frag, direct_links_block);
				if (indirect_links_block)
				{
					cur_frag = indirect_links_to_frags_list(&frags_list, 
							cur_frag, indirect_links_block);
					if (double_indirect_links_block)
						cur_frag = double_indirect_links_to_frags_list(&frags_list, 
								cur_frag, double_indirect_links_block);
				}
			}

			anysurrect_frags_list(frags_list, size64, 
					"filesystem_files/ext2fs/inode_table_blocks");
		}
		fd_seek(seek, SEEK_SET);
	}

	fd_seek(to_offset, SEEK_SET);
	return "filesystem_info/ext2fs/inode_table_blocks";
}

