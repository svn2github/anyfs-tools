/*
 *	filesystem_info_descr.c
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 *
 *      Some code from e2fsprogs
 *      Copyright (C) 1994, 1995, 1996 Theodore Ts'o
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

extern _declspec(dllexport)
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

extern _declspec(dllexport)
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

extern _declspec(dllexport)
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

extern _declspec(dllexport)
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

extern _declspec(dllexport)
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

extern _declspec(dllexport)
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
		{
			blocks[i] = COND_LELONG("direct_link", val <= device_blocks);

			int j;
			if (blocks[i])
				for (j=0; j<i; j++)
					if (blocks[i] == blocks[j])
						return NULL;
		}

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
		if ( links && !(mode) ) return NULL;

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

#include <ext2fs/ext2fs.h>

struct ext2fs_super_block
{
	uint32_t inodes_count;
	uint32_t blocks_count;
	uint32_t r_blocks_count;
	uint32_t free_blocks_count;
	uint32_t free_inodes_count;
	uint32_t first_data_block;
	uint32_t log_block_size;
	uint32_t log_frag_size;
	uint32_t blocks_per_group;
	uint32_t frags_per_group;
	uint32_t inodes_per_group;
	uint32_t mtime;
	uint32_t wtime;
	uint16_t mnt_count;
	uint16_t max_mnt_count;
	uint16_t magic;
	uint16_t state;
	uint16_t errors;
	uint16_t minor_rev_level;
	uint32_t lastcheck;
	uint32_t checkinterval;
	uint32_t creator_os;
	uint32_t rev_level;
	uint16_t def_resuid;
	uint16_t def_resgid;

	uint32_t first_ino;
	uint16_t inode_size;
	uint16_t block_group_nr;
	uint32_t feature_compat;
	uint32_t feature_incompat;
	uint32_t feature_ro_compat;

	uint16_t reserved_gdt_blocks;

	uint32_t first_meta_bg;
};

struct ext2fs_fs
{
	unsigned int 	blocksize;
	dgrp_t 		group_desc_count;
	unsigned long 	desc_blocks;
	int 		inode_blocks_per_group;
};

static int test_root(int a, int b)
{
	if (a == 0)
		return 1;
	while (1) {
		if (a == 1)
			return 1;
		if (a % b)
			return 0;
		a = a / b;
	}
}

int as_ext2fs_bg_has_super(struct ext2fs_super_block *sb, int group_block)
{
	if (!(sb->feature_ro_compat &
				EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER))
		return 1;

	if (test_root(group_block, 3) || (test_root(group_block, 5)) ||
			test_root(group_block, 7))
		return 1;

	return 0;
}

int ext2fs_super_and_bgd_size(dgrp_t group, struct ext2fs_super_block *sb, 
		struct ext2fs_fs *fs)
{
	blk_t	group_block, old_desc_blk = 0, new_desc_blk = 0;
	unsigned int meta_bg, meta_bg_size;
	int	numblocks = 0, has_super;
	int	old_desc_blocks;

	group_block = sb->first_data_block +
		(group * sb->blocks_per_group);

	if (sb->feature_incompat & EXT2_FEATURE_INCOMPAT_META_BG)
		old_desc_blocks = sb->first_meta_bg;
	else
		old_desc_blocks = 
			fs->desc_blocks + sb->reserved_gdt_blocks;

	has_super = as_ext2fs_bg_has_super(sb, group);

	if (has_super) numblocks++;
	meta_bg_size = (fs->blocksize / sizeof (struct ext2_group_desc));
	meta_bg = group / meta_bg_size;

	if (!(sb->feature_incompat & EXT2_FEATURE_INCOMPAT_META_BG) ||
	    (meta_bg < sb->first_meta_bg)) {
		if (has_super) {
			old_desc_blk = group_block + 1;
			numblocks += old_desc_blocks;
		}
	} else {
		if (((group % meta_bg_size) == 0) ||
		    ((group % meta_bg_size) == 1) ||
		    ((group % meta_bg_size) == (meta_bg_size-1))) {
			if (has_super)
				has_super = 1;
			new_desc_blk = group_block + has_super;
			numblocks++;
		}
	}
		
	numblocks += 2 + fs->inode_blocks_per_group;

	return (numblocks);
}

static int s_blocks_per_group = 0;
static int inode_ratio = 0;
static int num_inodes = 0;

void filesystem_info_ext2fs_group_info_parseopts
	(int argc, const char* argv[])
{
	const char * program_name = "filesystem_info_ext2fs_group_info_surrect";

	int c;
	char *tmp;

	optind--;
	int s_optind = optind;
	argv+=optind;
	argc-=optind;
	optind = 0;

	int endopts = 0;
	while ( !endopts && (c = getopt (argc, argv,
		    "g:i:N:h-")) != EOF ) {
		switch (c) {
		case 'g':
			s_blocks_per_group = strtoul(optarg, &tmp, 0);
			if (*tmp) {
				com_err(program_name, 0,
					_("Illegal number for blocks per group"));
				exit(1);
			}
			if ((s_blocks_per_group % 8) != 0) {
				com_err(program_name, 0,
				_("blocks per group must be multiple of 8"));
				exit(1);
			}
			break;
		case 'i':
			inode_ratio = strtoul(optarg, &tmp, 0);
			if (inode_ratio < EXT2_MIN_BLOCK_SIZE ||
			    inode_ratio > EXT2_MAX_BLOCK_SIZE * 1024 ||
			    *tmp) {
				com_err(program_name, 0,
					_("invalid inode ratio %s (min %d/max %d)"),
					optarg, EXT2_MIN_BLOCK_SIZE,
					EXT2_MAX_BLOCK_SIZE);
				exit(1);
			}
			break;
		case 'N':
			num_inodes = atoi(optarg);
			break;

		case '-':
			endopts = 1;
			break;

		case 'h':
		default:
			filesystem_info_ext2fs_group_info_usage();
			exit(1);
		}
	}

	argv -= s_optind;
	argc += s_optind;
	optind += s_optind;
	optind++;
};

void filesystem_info_ext2fs_group_info_usage()
{
	printf("filesystem_info_ext2fs_group_info surrecter usage:\n");
	printf(
"ext2fs [-g blocks-per-group] [ -i bytes-per-inode ] [-N  number-of-inodes]\n"
"	[-h] [--]\n");
	printf("\n");
};

extern _declspec(dllexport)
char *filesystem_info_ext2fs_group_info_surrect()
{
	static int done = 0;
	if (done) return NULL;

	struct ext2fs_super_block 	sb;
	struct ext2fs_fs		fs;

	sb.inodes_count = READ_LELONG("inodes_count");
	sb.blocks_count = READ_LELONG("blocks_count");
	sb.r_blocks_count = READ_LELONG("r_blocks_count");
	sb.free_blocks_count = READ_LELONG("free_blocks_count");
	sb.free_inodes_count = READ_LELONG("free_inodes_count");
	sb.first_data_block = READ_LELONG("first_data_block");
	sb.log_block_size = READ_LELONG("log_block_size");
	sb.log_frag_size = READ_LELONG("log_frag_size");
	sb.blocks_per_group = READ_LELONG("blocks_per_group");
	sb.frags_per_group = READ_LELONG("frags_per_group");
	sb.inodes_per_group = READ_LELONG("inodes_per_group");
	sb.mtime = READ_LELONG("mtime");
	sb.wtime = READ_LELONG("wtime");
	sb.mnt_count = READ_LESHORT("mnt_count");
	sb.max_mnt_count = READ_LESHORT("max_mnt_count");
	sb.magic = READ_LESHORT("magic");
	sb.state = READ_LESHORT("state");
	sb.errors = READ_LESHORT("errors");
	sb.minor_rev_level = READ_LESHORT("minor_rev_level");
	sb.lastcheck = READ_LELONG("lastcheck");
	sb.checkinterval = READ_LELONG("checkinterval");
	sb.creator_os = READ_LELONG("creator_os");
	sb.rev_level = READ_LELONG("rev_level");
	sb.def_resuid = READ_LESHORT("def_resuid");
	sb.def_resgid = READ_LESHORT("def_resgid");

	sb.first_ino = READ_LELONG("first_ino");
	sb.inode_size = READ_LESHORT("inode_size");
	sb.block_group_nr = READ_LESHORT("block_group_nr");
	sb.feature_compat = READ_LELONG("feature_compat");
	sb.feature_incompat = READ_LELONG("feature_incompat");
	sb.feature_ro_compat = READ_LELONG("feature_ro_compat");

	SKIP_STRING("uuid", 16);
	SKIP_STRING("volume_name", 16);
	SKIP_STRING("last_mounted", 64);
	SKIP_STRING("algorithm_usage_bitmap", 4);

	SKIP_STRING("prealloc_blocks", 1);
	SKIP_STRING("prealloc_dir_blocks", 1);
	sb.reserved_gdt_blocks = READ_LESHORT("reserved_gdt_blocks");

	SKIP_STRING("journal_uuid", 16);
	SKIP_STRING("journal_inum", 4);
	SKIP_STRING("journal_dev", 4);
	SKIP_STRING("last_orphan", 4);
	SKIP_STRING("hash_seed", 4*4);
	SKIP_STRING("def_hash_version", 1);
	SKIP_STRING("reserved_char_pad", 1);
	SKIP_STRING("reserved_word_pad", 2);
	SKIP_STRING("default_mount_opts", 4);
	sb.first_meta_bg = READ_LELONG("first_meta_bg");

	/*
	 * Calculate number of inodes based on the inode ratio
	 */
	sb.inodes_count = num_inodes ? num_inodes : 
		inode_ratio ? ((__u64) sb.blocks_count * blocksize) / 
		inode_ratio : sb.inodes_count;

	if (s_blocks_per_group)
		sb.blocks_per_group = s_blocks_per_group;

#define EXT2_SUPER_MAGIC      0xEF53
	if ( sb.magic != EXT2_SUPER_MAGIC ) return NULL;

	if ( !sb.inodes_count || !sb.blocks_count ) return NULL;
	if ( sb.inodes_count < sb.free_inodes_count || 
	  	sb.blocks_count < sb.free_blocks_count ) return NULL;

	if ( sb.first_data_block > 10 ) return NULL;

	if ( sb.log_block_size > 12 ) return NULL;
	if ( sb.blocks_per_group < 10 ) return NULL;

	if ( (get_block() - sb.first_data_block)%sb.blocks_per_group ) return NULL;

	fs.blocksize = get_blocksize();
	
	if (sb.feature_incompat & EXT3_FEATURE_INCOMPAT_JOURNAL_DEV)
		fs.group_desc_count = 0;
	else
	{
		fs.group_desc_count = (sb.blocks_count -
				sb.first_data_block +
				sb.blocks_per_group - 1)
			/ sb.blocks_per_group;
	}

	int desc_per_block = get_blocksize() / sizeof (struct ext2_group_desc);

	fs.desc_blocks = (fs.group_desc_count +
			desc_per_block - 1)
		/ desc_per_block;

	sb.inodes_per_group = (sb.inodes_count + fs.group_desc_count - 1) /
		fs.group_desc_count;

	fs.inode_blocks_per_group = (((sb.inodes_per_group *
					sb.inode_size) +
				get_blocksize() - 1) /
			get_blocksize());

	int gr, i, j;
	for (gr=1, i=sb.first_data_block + sb.blocks_per_group; 
			i < device_blocks; gr++, i+=sb.blocks_per_group)
	{
		struct frags_list *frags_list = NULL;
		struct frags_list *cur_frag = NULL;

		int super_and_bgd_size = ext2fs_super_and_bgd_size(
				gr, &sb, &fs);

		for (j=0; j < super_and_bgd_size; j++)
			cur_frag = addblock_to_frags_list(&frags_list, cur_frag, i+j);

		anysurrect_frags_list(frags_list, j * get_blocksize(), 
				"filesystem_files/ext2fs/group_info");
	}

	fd_seek( get_blocksize(), SEEK_SET );

	done = 1;

	return NULL;
}
 
