/*
 *	fs/any/any.h
 *	Copyright (C) 2005-2006 
 *		Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#ifndef _FS_ANY_ANY_H
#define _FS_ANY_ANY_H

#include <linux/fs.h>

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#error Kernel version less than 2.6.0 not supported
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
#warning Building with kernel version less than 2.6.9 not tested
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#  define KERNEL_2_6
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13)
#    define KERNEL_2_6_13_PLUS
#  endif
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
#    define KERNEL_2_6_18_PLUS
#  endif
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
#    define KERNEL_2_6_19_PLUS
#  endif
#endif

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

#ifndef CURRENT_TIME_SEC
#define CURRENT_TIME_SEC ((struct timespec) { xtime.tv_sec, 0 })
#endif

extern struct inode_operations any_file_inode_operations;
extern struct inode_operations any_dir_inode_operations;
extern struct file_operations any_file_operations;
extern struct file_operations any_dir_operations;

extern struct inode_operations any_symlink_inode_operations;

struct any_file_fragment {
	uint32_t	fr_start;
	uint32_t	fr_length;
};

struct any_file_frags {
	uint32_t			fr_nfrags;
	struct any_file_fragment	*fr_frags;
};

struct any_dirent {
	char*			d_name;
	uint32_t		d_inode;
	uint32_t		d_pos;
	struct any_dirent	*d_next;
};

struct any_dir {
	uint32_t			d_ndirents;
	struct any_dirent*	d_dirent;
	struct any_dirent*	d_last;
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
	char 		*si_itfilename;
	unsigned long 	si_blocksize;
	unsigned long 	si_inodes;
	unsigned long 	*si_inode_bitmap;
	struct any_inode *si_inode_table;
};

extern int any_setattr(struct dentry *dentry, struct iattr *attr);
extern void any_set_inode(struct inode *inode, dev_t rdev);

#endif /* _FS_ANY_ANY_H */
