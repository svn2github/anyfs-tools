/*
 *	build_it/any.h
 *	Copyright (C) 2005 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#ifndef _FS_ANY_ANY_H
#define _FS_ANY_ANY_H

#include "config.h"
#if (HAVE_LIBINTL_H == 1) && (HAVE_LOCALE_H == 1)
#define ENABLE_NLS
#endif

#ifdef ENABLE_NLS
#include <libintl.h>
#include <locale.h>
#define _(a) (gettext (a))
#ifdef gettext_noop
#define N_(a) gettext_noop (a)
#else
#define N_(a) (a)
#endif
#define P_(singular, plural, n) (ngettext (singular, plural, n))
#ifndef NLS_CAT_NAME
#define NLS_CAT_NAME "anyfs-tools"
#endif
#ifndef LOCALEDIR
#define LOCALEDIR PREFIX"/share/locale"
#endif
#else
#define _(a) (a)
#define N_(a) a
#define P_(singular, plural, n) ((n) == 1 ? (singular) : (plural))
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

//#include <linux/fs.h>
//#include <linux/types.h>
#include <stdint.h>

typedef uint32_t	any_blk_t;

typedef uint64_t  any_size_t;
typedef int64_t any_ssize_t;
typedef int64_t any_off_t;

/*min_t, max_t Macroses from Linux kernel*/
#define min_t(type,x,y) \
	        ({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#define max_t(type,x,y) \
	        ({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })

#define ANY_SUPER_MAGIC	0x414e59 /*ANY*/
#define ANY_BUFFER_SIZE 1024
#define ANY_NAME_LENGTH 255
#define ANY_LINK_MAX    50000
#define ANY_ITOPTIONS	"inodetable="

#define ANY_BLOCK_SIZE_HEAD	"BLOCK_SIZE "
#define ANY_INODES_HEAD		"INODES "
#define ANY_INODE_TABLE_HEAD	"INODE_TABLE\n"
#define ANY_DATA_HEAD		"DATA\n"
#define ANY_LNK_HEAD		"LNK "
#define ANY_REG_HEAD		"REG "
#define ANY_DIR_HEAD		"DIR "

#define ANY_BLOCK_SIZE_LEN	strlen(ANY_BLOCK_SIZE_HEAD"xxxxxxxx\n")
#define ANY_INODES_LEN		strlen(ANY_INODES_HEAD"xxxxxxxx\n")

#define ANY_INODE_TABLE_INFOLEN(inodes)		(inodes*73)

#define ANY_INODE_TABLE_LEN(inodes)	\
	(strlen(ANY_INODE_TABLE_HEAD)	+ ANY_INODE_TABLE_INFOLEN(inodes))

#define ANY_BLOCK_SIZE_HEAD_OFFSET	0
#define ANY_INODES_HEAD_OFFSET		\
	(ANY_BLOCK_SIZE_HEAD_OFFSET	+ ANY_BLOCK_SIZE_LEN)
#define ANY_INODE_TABLE_HEAD_OFFSET	\
	(ANY_INODES_HEAD_OFFSET		+ ANY_INODES_LEN)
#define ANY_DATA_HEAD_OFFSET(inodes)	\
	(ANY_INODE_TABLE_HEAD_OFFSET + ANY_INODE_TABLE_LEN(inodes))

#define ANY_BLOCK_SIZE_OFFSET		\
	(ANY_BLOCK_SIZE_HEAD_OFFSET	+ strlen(ANY_BLOCK_SIZE_HEAD))
#define ANY_INODES_OFFSET		\
	(ANY_INODES_HEAD_OFFSET		+ strlen(ANY_INODES_HEAD))
#define ANY_INODE_TABLE_OFFSET	\
	(ANY_INODE_TABLE_HEAD_OFFSET	+ strlen(ANY_INODE_TABLE_HEAD))
#define ANY_DATA_OFFSET(inodes)		\
	(ANY_DATA_HEAD_OFFSET(inodes)		+ strlen(ANY_DATA_HEAD))

struct any_file_fragment {
	uint32_t	fr_start;
	uint32_t	fr_length;
};

struct any_file_frags {
	uint32_t	fr_nfrags;
	struct any_file_fragment	*fr_frags;
};

struct any_dirent {
	char*	d_name;
	uint32_t	d_inode;
	struct any_dirent	*d_next;
};

struct any_dir {
	uint32_t			d_ndirents;
	struct any_dirent*	d_dirent;
};

struct any_inode {
	uint16_t  i_mode;         /* File mode */
	uint16_t  i_uid;          /* Low 16 bits of Owner Uid */
	uint16_t  i_gid;          /* Low 16 bits of Group Id */
	uint64_t  i_size;         /* Size in bytes */
	uint32_t  i_atime;        /* Access time */
	uint32_t  i_ctime;        /* Creation time */
	uint32_t  i_mtime;        /* Modification time */
	uint16_t  i_links_count;  /* Links count */
	union {
		struct any_file_frags	*file_frags;
		struct any_dir		*dir;
		char*	symlink;
		dev_t	device;
	} i_info;
	size_t	i_it_file_offset;
};

struct any_sb_info {
	char *si_itfilename;
	unsigned long si_blocksize;
	unsigned long si_inodes;
	unsigned long *si_inode_bitmap;
	struct any_inode *si_inode_table;
};

#endif /* _FS_ANY_ANY_H */
