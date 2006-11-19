/*
 *	fs/any/file.c
 *	Copyright (C) 2005 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#include <linux/buffer_head.h>          /* for fsync_inode_buffers() */
#include <linux/seq_file.h>
#include "any.h"

struct file_operations any_file_operations = {
	.llseek         = generic_file_llseek,
	.read           = generic_file_read,
	.write          = generic_file_write,
	.mmap           = generic_file_mmap,
	.sendfile       = generic_file_sendfile,
};

static int return_EOPNOTSUPP(void)
{
	return -EOPNOTSUPP;
}

#define EOPNOTSUPP_ERROR ((void *) (return_EOPNOTSUPP))

struct inode_operations any_file_inode_operations = {
	.truncate       = EOPNOTSUPP_ERROR,
	.setattr        = any_setattr,
	//.getattr        = any_getattr,
};

