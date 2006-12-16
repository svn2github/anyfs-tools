/*
 *	block_map.h
 *	(C) 2005-2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#ifndef _ANY_BLOCK_MAP_H
#define _ANY_BLOCK_MAP_H

#include "any.h"

int fill_block_bitmap (struct any_sb_info *info, unsigned long *block_bitmap, 
		any_blk_t dev_size);

#endif /*_ANY_BLOCK_MAP_H*/
