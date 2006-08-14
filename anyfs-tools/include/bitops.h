/*
 *	bitops.h
 *	Copyright (C) 2005 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */
#ifndef _ANY_BITOPS_H
#define _ANY_BITOPS_H

#include <ext2fs/ext2fs.h>

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

#endif /*_ANY_BITOPS_H*/
