/*
 *	block_map.h
 *	(C) 2005-2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#ifndef _ANY_BLOCK_MAP_H
#define _ANY_BLOCK_MAP_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "any.h"

int fill_block_bitmap (struct any_sb_info *info, unsigned long *block_bitmap, 
		any_blk_t dev_size, int check_intersects);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /*_ANY_BLOCK_MAP_H*/
