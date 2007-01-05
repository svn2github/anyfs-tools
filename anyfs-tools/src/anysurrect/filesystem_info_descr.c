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
#include "anysurrect.h"
#include "direct_io.h"
#include "filesystem_info_descr.h"

/*ext2fs direct blocks links*/
#include "any.h"
extern uint32_t get_blocksize();
extern any_size_t device_blocks;

char *filesystem_info_ext2fs_direct_blocks_links_surrect()
{
#define ERROR_VALUE	0
	int res;
	
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

	return "filesystem_info/ext2fs/direct_blocks_links";
#undef	ERROR_VALUE
}

char *filesystem_info_ext2fs_direct_blocks_links_surrect_dr()
{
#define ERROR_VALUE	0
	int res;
	
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
#undef	ERROR_VALUE
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

char *filesystem_info_ext2fs_indirect_blocks_links_surrect()
{
#define ERROR_VALUE	0
	int res;
	
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

	return "filesystem_info/ext2fs/indirect_blocks_links";
#undef	ERROR_VALUE
}

char *filesystem_info_ext2fs_indirect_blocks_links_surrect_dr()
{
#define ERROR_VALUE	0
	int res;
	
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
#undef	ERROR_VALUE
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

char *filesystem_info_ext2fs_double_indirect_blocks_links_surrect()
{
#define ERROR_VALUE	0
	int res;
	
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
#undef	ERROR_VALUE
}
