/*
 *	bitops.h
 *	Copyright (C) 2005-2006
 *		Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */
#ifndef _ANY_BITOPS_H
#define _ANY_BITOPS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(__linux__)
#include <ext2fs/ext2fs.h>
#endif

#define test_and_set_bit(nr,addr) \
	ext2fs_set_bit((nr),(unsigned long*)addr)
#define set_bit(nr,addr) test_and_set_bit((nr),(unsigned long*)addr)
#define test_and_clear_bit(nr,addr) \
	ext2fs_clear_bit((nr), (unsigned long*)addr)
#define clear_bit(nr,addr) test_and_clear_bit((nr),(unsigned long*)addr)
#define test_bit(nr,addr) ext2fs_test_bit((nr), (unsigned long*)addr)
#define find_first_zero_bit(addr, size) \
	find_next_zero_bit(addr, size, 0)

#include "find_next_zero_bit.h"

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /*_ANY_BITOPS_H*/
